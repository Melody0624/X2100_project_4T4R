src-y += arch/sys_arch.c

# COREFILES, CORE4FILES: The minimum set of files needed for lwIP.
COREFILES=core/init.c \
	core/def.c \
	core/dns.c \
	core/inet_chksum.c \
	core/ip.c \
	core/mem.c \
	core/memp.c \
	core/netif.c \
	core/pbuf.c \
	core/raw.c \
	core/stats.c \
	core/sys.c \
	core/altcp.c \
	core/altcp_alloc.c \
	core/altcp_tcp.c \
	core/tcp.c \
	core/tcp_in.c \
	core/tcp_out.c \
	core/timeouts.c \
	core/udp.c

CORE4FILES=core/ipv4/acd.c \
	core/ipv4/autoip.c \
	core/ipv4/dhcp.c \
	core/ipv4/etharp.c \
	core/ipv4/icmp.c \
	core/ipv4/igmp.c \
	core/ipv4/ip4_frag.c \
	core/ipv4/ip4.c \
	core/ipv4/ip4_addr.c

CORE6FILES=core/ipv6/dhcp6.c \
	core/ipv6/ethip6.c \
	core/ipv6/icmp6.c \
	core/ipv6/inet6.c \
	core/ipv6/ip6.c \
	core/ipv6/ip6_addr.c \
	core/ipv6/ip6_frag.c \
	core/ipv6/mld6.c \
	core/ipv6/nd6.c

# APIFILES: The files which implement the sequential and socket APIs.
APIFILES=api/api_lib.c \
	api/api_msg.c \
	api/err.c \
	api/if_api.c \
	api/netbuf.c \
	api/netdb.c \
	api/netifapi.c \
	api/sockets.c \
	api/tcpip.c

# NETIFFILES: Files implementing various generic network interface functions
NETIFFILES=netif/ethernet.c \
	netif/bridgeif.c \
	netif/bridgeif_fdb.c \
	netif/slipif.c

# SIXLOWPAN: 6LoWPAN
SIXLOWPAN=netif/lowpan6_common.c \
        netif/lowpan6.c \
	netif/lowpan6_ble.c \
	netif/zepif.c

# PPPFILES: PPP
PPPFILES=netif/ppp/auth.c \
	netif/ppp/ccp.c \
	netif/ppp/chap-md5.c \
	netif/ppp/chap_ms.c \
	netif/ppp/chap-new.c \
	netif/ppp/demand.c \
	netif/ppp/eap.c \
	netif/ppp/ecp.c \
	netif/ppp/eui64.c \
	netif/ppp/fsm.c \
	netif/ppp/ipcp.c \
	netif/ppp/ipv6cp.c \
	netif/ppp/lcp.c \
	netif/ppp/magic.c \
	netif/ppp/mppe.c \
	netif/ppp/multilink.c \
	netif/ppp/ppp.c \
	netif/ppp/pppapi.c \
	netif/ppp/pppcrypt.c \
	netif/ppp/pppoe.c \
	netif/ppp/pppol2tp.c \
	netif/ppp/pppos.c \
	netif/ppp/upap.c \
	netif/ppp/utils.c \
	netif/ppp/vj.c \
	netif/ppp/polarssl/arc4.c \
	netif/ppp/polarssl/des.c \
	netif/ppp/polarssl/md4.c \
	netif/ppp/polarssl/md5.c \
	netif/ppp/polarssl/sha1.c

# LWIPNOAPPSFILES: All LWIP files without apps
LWIPNOAPPSFILES=$(COREFILES) \
	$(CORE4FILES) \
	$(CORE6FILES) \
	$(APIFILES) \
	$(NETIFFILES) \
	$(PPPFILES) \
	$(SIXLOWPAN)

src-y += $(LWIPNOAPPSFILES)


# SNMPFILES: SNMPv2c agent
SNMPFILES=apps/snmp/snmp_asn1.c \
	apps/snmp/snmp_core.c \
	apps/snmp/snmp_mib2.c \
	apps/snmp/snmp_mib2_icmp.c \
	apps/snmp/snmp_mib2_interfaces.c \
	apps/snmp/snmp_mib2_ip.c \
	apps/snmp/snmp_mib2_snmp.c \
	apps/snmp/snmp_mib2_system.c \
	apps/snmp/snmp_mib2_tcp.c \
	apps/snmp/snmp_mib2_udp.c \
	apps/snmp/snmp_snmpv2_framework.c \
	apps/snmp/snmp_snmpv2_usm.c \
	apps/snmp/snmp_msg.c \
	apps/snmp/snmpv3.c \
	apps/snmp/snmp_netconn.c \
	apps/snmp/snmp_pbuf_stream.c \
	apps/snmp/snmp_raw.c \
	apps/snmp/snmp_scalar.c \
	apps/snmp/snmp_table.c \
	apps/snmp/snmp_threadsync.c \
	apps/snmp/snmp_traps.c

# HTTPFILES: HTTP server + client
HTTPFILES=apps/http/altcp_proxyconnect.c \
	apps/http/fs.c \
	apps/http/http_client.c \
	apps/http/httpd.c

# MAKEFSDATA: MAKEFSDATA HTTP server host utility
MAKEFSDATAFILES=apps/http/makefsdata/makefsdata.c

# LWIPERFFILES: IPERF server
LWIPERFFILES=apps/lwiperf/lwiperf.c

# SMTPFILES: SMTP client
SMTPFILES=apps/smtp/smtp.c

# SNTPFILES: SNTP client
SNTPFILES=apps/sntp/sntp.c

# MDNSFILES: MDNS responder
MDNSFILES=apps/mdns/mdns.c \
	apps/mdns/mdns_out.c \
	apps/mdns/mdns_domain.c

# NETBIOSNSFILES: NetBIOS name server
NETBIOSNSFILES=apps/netbiosns/netbiosns.c

# TFTPFILES: TFTP client/server files
TFTPFILES=apps/tftp/tftp.c

# MQTTFILES: MQTT client files
MQTTFILES=apps/mqtt/mqtt.c

# MBEDTLS_FILES: MBEDTLS related files of lwIP rep
MBEDTLS_FILES=apps/altcp_tls/altcp_tls_mbedtls.c \
	apps/altcp_tls/altcp_tls_mbedtls_mem.c \
	apps/snmp/snmpv3_mbedtls.c

# DHCPFILES: DHCP server files
DHCPFILES=apps/dhcp/dhcp_server.c

# LWIPAPPFILES: All LWIP APPs
LWIPAPPFILES=$(SNMPFILES) \
	$(HTTPFILES) \
	$(LWIPERFFILES) \
	$(SMTPFILES) \
	$(SNTPFILES) \
	$(MDNSFILES) \
	$(NETBIOSNSFILES) \
	$(TFTPFILES) \
	$(MQTTFILES) \
	$(MBEDTLS_FILES) \
	$(DHCPFILES)

src-$(CONFIG_NET_LWIP_APPS) += $(LWIPAPPFILES)
