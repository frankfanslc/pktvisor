/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "DnstapInputStream.h"
#include <catch2/catch.hpp>
#include <filesystem>
#include <fstrm/fstrm.h>
#include <uvw/async.h>
#include <uvw/loop.h>
#include <uvw/pipe.h>
#include <uvw/stream.h>

namespace visor::input::dnstap {

DnstapInputStream::DnstapInputStream(const std::string &name)
    : visor::InputStream(name)
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    _logger = spdlog::get("visor");
    assert(_logger);
}

void DnstapInputStream::_read_frame_stream_file()
{
    assert(config_exists("dnstap_file"));

    // Setup file reader options
    auto fileOptions = fstrm_file_options_init();
    fstrm_file_options_set_file_path(fileOptions, config_get<std::string>("dnstap_file").c_str());

    // Initialize file reader
    auto reader = fstrm_file_reader_init(fileOptions, nullptr);
    if (!reader) {
        throw DnstapException("fstrm_file_reader_init() failed");
    }
    auto result = fstrm_reader_open(reader);
    if (result != fstrm_res_success) {
        throw DnstapException("fstrm_reader_open() failed");
    }

    // Cleanup
    fstrm_file_options_destroy(&fileOptions);

    // Loop over data frames
    for (;;) {
        const uint8_t *data;
        size_t len_data;

        result = fstrm_reader_read(reader, &data, &len_data);
        if (result == fstrm_res_success) {
            // Data frame ready, parse protobuf
            ::dnstap::Dnstap d;
            if (!d.ParseFromArray(data, len_data)) {
                _logger->warn("Dnstap::ParseFromArray fail, skipping frame of size {}", len_data);
                continue;
            }
            if (!d.has_type() || d.type() != ::dnstap::Dnstap_Type_MESSAGE || !d.has_message()) {
                _logger->warn("dnstap data is wrong type or has no message, skipping frame of size {}", len_data);
                continue;
            }
            // Emit signal to handlers
            dnstap_signal(d);
        } else if (result == fstrm_res_stop) {
            // Normal end of data stream
            break;
        } else {
            // Abnormal end
            _logger->warn("fstrm_reader_read() data stream ended abnormally: {}", result);
            break;
        }
    }

    fstrm_reader_destroy(&reader);
}

void DnstapInputStream::start()
{

    if (_running) {
        return;
    }

    if (config_exists("dnstap_file")) {
        // read from dnstap file. this is a special case from a command line utility
        _running = true;
        _read_frame_stream_file();
        return;
    } else if (config_exists("socket")) {
        _create_frame_stream_socket();
    } else {
        throw DnstapException("config must specify one of: socket, dnstap_file");
    }

    _running = true;
}

void DnstapInputStream::_create_frame_stream_socket()
{
    assert(config_exists("socket"));

    // main io loop, run in its own thread
    _io_loop = uvw::Loop::create();
    if (!_io_loop) {
        throw DnstapException("unable to create io loop");
    }
    // AsyncHandle lets us stop the loop from its own thread
    _async_h = _io_loop->resource<uvw::AsyncHandle>();
    _async_h->on<uvw::AsyncEvent>([this](const auto &, auto &hndl) {
        _io_loop->stop();
        _io_loop->close();
        hndl.close();
    });
    _async_h->on<uvw::ErrorEvent>([this](const auto &err, auto &) {
        _logger->error("[{}] AsyncEvent error: {}", _name, err.what());
    });

    // setup socket handler
    auto server = _io_loop->resource<uvw::PipeHandle>();

    server->on<uvw::ErrorEvent>([this](const auto &err, auto &) {
        _logger->error("[{}] PipeHandle error: {}", _name, err.what());
    });

    server->once<uvw::ListenEvent>([this](const uvw::ListenEvent &l, uvw::PipeHandle &handle) {
        _logger->info("[{}]: dnstap client connected", _name);
        auto on_frame_stream_err = [this](const std::string &err) {
            _logger->error("[{}]: frame stream error: {}", _name, err);
        };
        bool ready{false}, finished{false};
        auto on_control_ready = [&ready]() { ready = true; };
        auto on_control_finished = [&finished]() { finished = true; };
        FrameSessionData session(
            CONTENT_TYPE, [this](const void *data, std::size_t len_data) {
                // Data frame ready, parse protobuf
                ::dnstap::Dnstap d;
                if (!d.ParseFromArray(data, len_data)) {
                    _logger->warn("Dnstap::ParseFromArray fail, skipping frame of size {}", len_data);
                    return;
                }
                if (!d.has_type() || d.type() != ::dnstap::Dnstap_Type_MESSAGE || !d.has_message()) {
                    _logger->warn("dnstap data is wrong type or has no message, skipping frame of size {}", len_data);
                    return;
                }
                // Emit signal to handlers
                dnstap_signal(d);
            },
            on_frame_stream_err, on_control_ready, on_control_finished);
        std::shared_ptr<uvw::PipeHandle> socket = handle.loop().resource<uvw::PipeHandle>();

        socket->on<uvw::ErrorEvent>([this](const uvw::ErrorEvent &err, const uvw::PipeHandle &) {
            _logger->error("[{}]: socket PipeHandle error: {}", _name, err.what());
        });
        socket->on<uvw::DataEvent>([this, &session](const uvw::DataEvent &data, uvw::PipeHandle &sock) {
            auto result = session.receive_socket_data(data.data.get(), data.length);
            if (!result) {
                // error in the session - close it
                sock.close();
            }
        });
        socket->on<uvw::CloseEvent>([&handle](const uvw::CloseEvent &, uvw::PipeHandle &) { handle.close(); });
        socket->on<uvw::EndEvent>([this](const uvw::EndEvent &, uvw::PipeHandle &sock) {
            _logger->info("[{}]: dnstap client disconnected", _name);
            sock.close();
        });

        handle.accept(*socket);
        socket->read();
    });

    // attempt to remove socket if it exists, ignore errors
    std::filesystem::remove(config_get<std::string>("socket"));

    server->bind(config_get<std::string>("socket"));
    server->listen();

    // spawn the loop
    _io_thread = std::make_unique<std::thread>([this] {
        _io_loop->run();
    });
}

void DnstapInputStream::stop()
{
    if (!_running) {
        return;
    }

    if (_async_h && _io_thread) {
        // we have to use AsyncHandle to stop the loop from the same thread the loop is running in
        _async_h->send();
        // waits for _io_loop->run() to return
        _io_thread->join();
    }

    _running = false;
}

void DnstapInputStream::info_json(json &j) const
{
    common_info_json(j);
}

bool FrameSessionData::decode_control_frame(const void *control_frame, size_t len_control_frame)
{
    fstrm_res res;
    fstrm_control_type c_type;
    struct fstrm_control *c;
    uint32_t flags = 0;
    c = fstrm_control_init();
    res = fstrm_control_decode(c, control_frame, len_control_frame, flags);
    if (res != fstrm_res_success) {
        puts("fstrm_control_decode() failed.");
        fstrm_control_destroy(&c);
        return false;
    }
    res = fstrm_control_get_type(c, &c_type);
    if (res != fstrm_res_success) {
        puts("fstrm_control_get_type() failed.");
        fstrm_control_destroy(&c);
        return false;
    }
    printf("The control frame is of type %s (%u).\n",
        fstrm_control_type_to_str(c_type), c_type);

    switch (c_type) {
        // uni-directional
    case FSTRM_CONTROL_START: {
        if (_state != FrameState::New) {
            _on_frame_stream_err_cb("received START frame but already started, aborting");
            return false;
        } else {
            _state = FrameState::Running;
        }
    }
        // bi-directional
    case FSTRM_CONTROL_READY: {
        if (_state != FrameState::New) {
            _on_frame_stream_err_cb("received READY frame but already started, aborting");
            return false;
        } else {
            _state = FrameState::Ready;
            _is_bidir = true;
            _on_control_ready_cb();
        }
    }
    }

    size_t n_content_type;
    res = fstrm_control_get_num_field_content_type(c, &n_content_type);
    if (res != fstrm_res_success) {
        puts("fstrm_control_get_num_field_content_type() failed.");
        fstrm_control_destroy(&c);
        return false;
    }
    const uint8_t *content_type;
    size_t len_content_type;
    for (size_t idx = 0; idx < n_content_type; idx++) {
        res = fstrm_control_get_field_content_type(c, idx,
            &content_type, &len_content_type);
        if (res == fstrm_res_success) {
            printf("The control frame has a CONTENT_TYPE field of length %zd.\n",
                len_content_type);
        }
        // TODO check content_type
    }
    fstrm_control_destroy(&c);
    return true;
}

// for reference, see fstrm__reader_next_data from fstrm library
bool FrameSessionData::receive_socket_data(const char data[], std::size_t data_len)
{
    _buffer.append(data, data_len);

    std::uint32_t frame_len{0};

    if (_buffer.size() < sizeof(frame_len)) {
        _on_frame_stream_err_cb("invalid data: header length");
        return false;
    }

    std::memcpy(&frame_len, _buffer.data(), sizeof(frame_len));
    frame_len = ntohl(frame_len);

    if (frame_len != 0) {
        // this is a data frame and we have the length
        if (_state != FrameState::Running) {
            // we got a data frame but we never saw a START control frame, abort
            _on_frame_stream_err_cb("stream had data frame with a START control frame");
            return false;
        }

        // ensure we never allocate more than max
        if (frame_len > FSTRM_READER_MAX_FRAME_SIZE_DEFAULT) {
            _on_frame_stream_err_cb("data frame too large");
            return false;
        }

        if (_buffer.size() >= sizeof(frame_len) + frame_len) {
            auto data = std::make_unique<uint8_t[]>(frame_len);
            std::memcpy(data.get(), _buffer.data() + sizeof(frame_len), frame_len);
            _buffer.erase(0, sizeof(frame_len) + frame_len);
            //_on_data_frame_cb(std::move(data), len);
        }
    } else {
        // this is a control frame
        // note this happens infrequently

        _buffer.erase(0, sizeof(frame_len)); // erase escape code

        // get control frame length
        std::uint32_t ctrl_len{0};

        if (_buffer.size() < sizeof(ctrl_len)) {
            _on_frame_stream_err_cb("invalid data: control frame length");
            return false;
        }

        std::memcpy(&ctrl_len, _buffer.data(), sizeof(ctrl_len));
        ctrl_len = ntohl(ctrl_len);

        if (_buffer.size() >= sizeof(ctrl_len) + ctrl_len) {
            if (!decode_control_frame(_buffer.data() + sizeof(ctrl_len), ctrl_len)) {
                _on_frame_stream_err_cb("unable to parse control frame");
                return false;
            }
            _buffer.erase(0, sizeof(ctrl_len) + ctrl_len);
        }
    }
    // parsed ok, wait for more data
    return true;
}

}
