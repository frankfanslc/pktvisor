#include <catch2/catch.hpp>

#include "GeoDB.h"
#include "NetStreamHandler.h"
#include "PcapInputStream.h"

using namespace visor::handler::net;
using namespace visor::input::pcap;

TEST_CASE("Parse net (dns) UDP IPv4 tests", "[pcap][ipv4][udp][net]")
{

    PcapInputStream stream{"pcap-test"};
    stream.config_set("pcap_file", "tests/fixtures/dns_ipv4_udp.pcap");
    stream.config_set("bpf", std::string());

    visor::Config c;
    c.config_set<uint64_t>("num_periods", 1);
    NetStreamHandler net_handler{"net-test", &stream, &c};

    net_handler.start();
    stream.start();
    net_handler.stop();
    stream.stop();

    auto counters = net_handler.metrics()->bucket(0)->counters();
    auto event_data = net_handler.metrics()->bucket(0)->event_data_locked();

    CHECK(net_handler.metrics()->current_periods() == 1);
    CHECK(net_handler.metrics()->start_tstamp().tv_sec == 1567706414);
    CHECK(net_handler.metrics()->start_tstamp().tv_nsec == 599964000);

    CHECK(net_handler.metrics()->end_tstamp().tv_sec == 1567706420);
    CHECK(net_handler.metrics()->end_tstamp().tv_nsec == 602866000);

    CHECK(net_handler.metrics()->bucket(0)->period_length() == 6);

    CHECK(event_data.num_events->value() == 140);
    CHECK(counters.UDP.value() == 140);
    CHECK(counters.IPv4.value() == 140);
    CHECK(counters.IPv6.value() == 0);
}

TEST_CASE("Parse net (dns) TCP IPv4 tests", "[pcap][ipv4][tcp][net]")
{
    PcapInputStream stream{"pcap-test"};
    stream.config_set("pcap_file", "tests/fixtures/dns_ipv4_tcp.pcap");
    stream.config_set("bpf", "");

    visor::Config c;
    c.config_set<uint64_t>("num_periods", 1);
    NetStreamHandler net_handler{"net-test", &stream, &c};

    net_handler.start();
    stream.start();
    net_handler.stop();
    stream.stop();

    auto counters = net_handler.metrics()->bucket(0)->counters();
    auto event_data = net_handler.metrics()->bucket(0)->event_data_locked();

    CHECK(net_handler.metrics()->start_tstamp().tv_sec == 1567706433);
    CHECK(net_handler.metrics()->start_tstamp().tv_nsec == 56403000);
    CHECK(event_data.num_events->value() == 2100);
    CHECK(counters.TCP.value() == 2100);
    CHECK(counters.IPv4.value() == 2100);
    CHECK(counters.IPv6.value() == 0);
}

TEST_CASE("Parse net (dns) UDP IPv6 tests", "[pcap][ipv6][udp][net]")
{

    PcapInputStream stream{"pcap-test"};
    stream.config_set("pcap_file", "tests/fixtures/dns_ipv6_udp.pcap");
    stream.config_set("bpf", "");

    visor::Config c;
    c.config_set<uint64_t>("num_periods", 1);
    NetStreamHandler net_handler{"net-test", &stream, &c};

    net_handler.start();
    stream.start();
    stream.stop();
    net_handler.stop();

    auto counters = net_handler.metrics()->bucket(0)->counters();
    auto event_data = net_handler.metrics()->bucket(0)->event_data_locked();

    CHECK(net_handler.metrics()->start_tstamp().tv_sec == 1567706365);
    CHECK(net_handler.metrics()->start_tstamp().tv_nsec == 513271000);
    CHECK(event_data.num_events->value() == 140);
    CHECK(counters.UDP.value() == 140);
    CHECK(counters.IPv4.value() == 0);
    CHECK(counters.IPv6.value() == 140);
}

TEST_CASE("Parse net (dns) TCP IPv6 tests", "[pcap][ipv6][tcp][net]")
{

    PcapInputStream stream{"pcap-test"};
    stream.config_set("pcap_file", "tests/fixtures/dns_ipv6_tcp.pcap");
    stream.config_set("bpf", "");

    visor::Config c;
    c.config_set<uint64_t>("num_periods", 1);
    NetStreamHandler net_handler{"net-test", &stream, &c};

    net_handler.start();
    stream.start();
    stream.stop();
    net_handler.stop();

    auto counters = net_handler.metrics()->bucket(0)->counters();
    auto event_data = net_handler.metrics()->bucket(0)->event_data_locked();

    CHECK(net_handler.metrics()->start_tstamp().tv_sec == 1567706308);
    CHECK(net_handler.metrics()->start_tstamp().tv_nsec == 958184000);
    CHECK(event_data.num_events->value() == 1800);
    CHECK(counters.TCP.value() == 1800);
    CHECK(counters.IPv4.value() == 0);
    CHECK(counters.IPv6.value() == 1800);
}

TEST_CASE("Parse net (dns) random UDP/TCP tests", "[pcap][net]")
{

    PcapInputStream stream{"pcap-test"};
    stream.config_set("pcap_file", "tests/fixtures/dns_udp_tcp_random.pcap");
    stream.config_set("bpf", "");
    stream.config_set("host_spec", "192.168.0.0/24");
    stream.parse_host_spec();

    visor::Config c;
    c.config_set<uint64_t>("num_periods", 1);
    NetStreamHandler net_handler{"net-test", &stream, &c};

    net_handler.start();
    stream.start();
    stream.stop();
    net_handler.stop();

    auto counters = net_handler.metrics()->bucket(0)->counters();
    auto event_data = net_handler.metrics()->bucket(0)->event_data_locked();

    CHECK(net_handler.metrics()->start_tstamp().tv_sec == 1614874231);
    CHECK(net_handler.metrics()->start_tstamp().tv_nsec == 565771000);

    // confirmed with wireshark
    CHECK(event_data.num_events->value() == 16147);
    CHECK(event_data.num_samples->value() == 16147);
    CHECK(counters.TCP.value() == 13176);
    CHECK(counters.UDP.value() == 2971);
    CHECK(counters.IPv4.value() == 16147);
    CHECK(counters.IPv6.value() == 0);
    CHECK(counters.OtherL4.value() == 0);
    CHECK(counters.total_in.value() == 6648);
    CHECK(counters.total_out.value() == 9499);

    nlohmann::json j;
    net_handler.metrics()->bucket(0)->to_json(j);

    CHECK(j["cardinality"]["dst_ips_out"] == 1);
    CHECK(j["cardinality"]["src_ips_in"] == 1);
    CHECK(j["top_ipv4"][0]["estimate"] == 16147);
    CHECK(j["top_ipv4"][0]["name"] == "8.8.8.8");
}

TEST_CASE("Parse net (dns) with DNS filter only_qname_suffix", "[pcap][dns][net]")
{

    PcapInputStream stream{"pcap-test"};
    stream.config_set("pcap_file", "tests/fixtures/dns_udp_mixed_rcode.pcap");
    stream.config_set("bpf", "");
    stream.config_set("host_spec", "192.168.0.0/24");
    stream.parse_host_spec();

    visor::Config c;
    c.config_set<uint64_t>("num_periods", 1);
    DnsStreamHandler dns_handler{"dns-test", &stream, &c};
    NetStreamHandler net_handler{"net-test", nullptr, &c, &dns_handler};

    dns_handler.config_set<visor::Configurable::StringList>("only_qname_suffix", {"google.com"});

    net_handler.start();
    dns_handler.start();
    stream.start();
    stream.stop();
    dns_handler.stop();
    net_handler.stop();

    auto dns_counters = dns_handler.metrics()->bucket(0)->counters();
    CHECK(dns_counters.UDP.value() == 10);
    CHECK(dns_counters.IPv4.value() == 10);

    auto net_counters = net_handler.metrics()->bucket(0)->counters();
    auto event_data = net_handler.metrics()->bucket(0)->event_data_locked();

    CHECK(event_data.num_events->value() == 10);
    CHECK(net_counters.TCP.value() == 0);
    CHECK(net_counters.UDP.value() == 10);
    CHECK(net_counters.IPv4.value() == 10);

    nlohmann::json j;
    net_handler.metrics()->bucket(0)->to_json(j);

    CHECK(j["cardinality"]["dst_ips_out"] == 3);
    CHECK(j["cardinality"]["src_ips_in"] == 5);
    CHECK(j["top_ipv4"][0]["estimate"] == 4);
    CHECK(j["top_ipv4"][0]["name"] == "216.239.38.10");
}

TEST_CASE("Parse net (dns) sflow stream", "[sflow][net]")
{

    SflowInputStream stream{"sflow-test"};
    stream.config_set("pcap_file", "tests/fixtures/ecmp.pcap");

    visor::Config c;
    c.config_set<uint64_t>("num_periods", 1);
    NetStreamHandler net_handler{"net-test", &stream, &c};

    net_handler.start();
    stream.start();
    stream.stop();
    net_handler.stop();

    auto counters = net_handler.metrics()->bucket(0)->counters();
    auto event_data = net_handler.metrics()->bucket(0)->event_data_locked();

    // confirmed with wireshark
    CHECK(event_data.num_events->value() == 9279);
    CHECK(event_data.num_samples->value() == 9279);
    CHECK(counters.TCP.value() == 52785);
    CHECK(counters.UDP.value() == 0);
    CHECK(counters.IPv4.value() == 52785);
    CHECK(counters.IPv6.value() == 0);
    CHECK(counters.OtherL4.value() == 3682);
    CHECK(counters.total_in.value() == 0);
    CHECK(counters.total_out.value() == 0);

    nlohmann::json j;
    net_handler.metrics()->bucket(0)->to_json(j);

    CHECK(j["cardinality"]["dst_ips_out"] == 4);
    CHECK(j["cardinality"]["src_ips_in"] == 4);
    CHECK(j["top_ipv4"][0]["estimate"] == 27054);
    CHECK(j["top_ipv4"][0]["name"] == "10.4.2.2");
}