mod dns_api;

pub mod dns_interceptor {
    use base64::{engine::general_purpose, Engine as _};
    use pcap::Capture;
    use dns_parser::{Packet as DnsPacket, Opcode};
    use std::collections::HashMap;
    use std::net::IpAddr;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::{Arc, Mutex};
    use std::thread;
    use std::time::Duration;
    use serde_json::json;

    #[derive(Debug, Clone)]
    pub struct DnsRequest {
        pub query_name: String,
        pub source_ip: IpAddr,
        pub source_port: u16,
        pub qtype: dns_parser::QueryType,
        pub qclass: dns_parser::QueryClass,
        pub header: dns_parser::Header,
        pub raw_payload: Vec<u8>,
    }

    impl std::fmt::Display for DnsRequest {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            let h = &self.header;
            write!(
                f,
                "{} from {}:{} | id={} QR={} opcode={:?} rcode={:?} qtype={:?} rd={} qd={} an={}",
                self.query_name,
                self.source_ip,
                self.source_port,
                h.id,
                if h.query { 0 } else { 1 },
                h.opcode,
                h.response_code,
                self.qtype,
                h.recursion_desired as u8,
                h.questions,
                h.answers,
            )?;
            Ok(())
        }
    }

    #[derive(Debug, Clone)]
    pub struct Stats {
        pub total_requests: usize,
        pub requests_by_domain: HashMap<String, usize>,
    }

    impl Stats {
        pub fn new() -> Self {
            Stats {
                total_requests: 0,
                requests_by_domain: HashMap::new(),
            }
        }

        pub fn increment_total(&mut self) {
            self.total_requests += 1;
        }

        pub fn add_request_by_domain(&mut self, domain: String) {
            *self.requests_by_domain.entry(domain).or_insert(0) += 1;
        }

        pub fn get_total_requests(&self) -> usize {
            self.total_requests
        }

        pub fn get_requests_by_domain(&self) -> &HashMap<String, usize> {
            &self.requests_by_domain
        }
    }

    #[derive(Debug)]
    pub struct Args {
        pub debug: bool,
        pub api_port: u16,
    }

    impl Args {
        pub fn parse() -> Self {
            let mut debug = false;
            let mut api_port = 9090;

            let args: Vec<String> = std::env::args().collect();
            for i in 0..args.len() {
                if args[i] == "--debug" || args[i] == "-d" {
                    debug = true;
                }
                if args[i] == "--api-port" && i + 1 < args.len() {
                    api_port = args[i + 1].parse().unwrap_or(9090);
                }
            }

            Args { debug, api_port }
        }
    }

    fn print_final_stats(stats: &Stats) {
        println!("\n--- Final DNS Statistics ---");
        println!("Total requests: {}", stats.total_requests);

        let mut domains: Vec<_> = stats.requests_by_domain.iter().collect();
        domains.sort_by(|a, b| b.1.cmp(a.1));
        println!("Top domains:");
        for (domain, count) in domains.iter().take(5) {
            println!("  {}: {} requests", domain, count);
        }
    }

    pub fn start_api_server(stats: Arc<Mutex<Stats>>, port: u16) {
        let api_filter = crate::dns_api::api_filter(stats);

        println!("Starting API server on port {}", port);
        std::thread::spawn(move || {
            tokio::runtime::Runtime::new().unwrap().block_on(async move {
                warp::serve(api_filter)
                    .run(([127, 0, 0, 1], port))
                    .await;
            });
        });
    }

    pub fn main() {
        let args = Args::parse();

        println!("DNS Interceptor - Starting...");
        if args.debug {
            println!("Debug mode enabled - non-DNS packet messages will be hidden");
        }

        let mut cap = Capture::from_device("enp8s0")
            .unwrap()
            .immediate_mode(true)
            .timeout(1000)
            .open()
            .expect("Failed to open capture");

        cap.filter("udp port 53", true)
            .expect("Failed to set filter for DNS packets");

        let running = Arc::new(AtomicBool::new(true));
        let r = running.clone();

        ctrlc::set_handler(move || {
            println!(" Ya ya ya se esta cerrando...");
            r.store(false, Ordering::SeqCst);
        })
        .expect("Imposible de cerrar, lo siento, es mas sudo que yo");

        let stats = Arc::new(Mutex::new(Stats::new()));
        let stats_for_thread = Arc::clone(&stats);

        let running_for_stats = running.clone();
        let stats_thread = thread::spawn(move || {
            loop {
                thread::sleep(Duration::from_secs(5));

                if !running_for_stats.load(Ordering::SeqCst) {
                    break;
                }

                let stats = stats_for_thread.lock().unwrap();
                println!("\n--- DNS Statistics ---");
                println!("Total requests: {}", stats.total_requests);

                let mut domains: Vec<_> = stats.requests_by_domain.iter().collect();
                domains.sort_by(|a, b| b.1.cmp(a.1));
                println!("Top domains:");
                for (domain, count) in domains.iter().take(5) {
                    println!("  {}: {} requests", domain, count);
                }
            }
        });

        let api_stats = Arc::clone(&stats);
        let api_port = args.api_port;
        let _api_thread = thread::spawn(move || {
            start_api_server(api_stats, api_port);
        });

        let el_http = reqwest::blocking::Client::builder()
            .timeout(std::time::Duration::from_secs(5))
            .build()
            .expect("Error capa 8 no se que significa pero lo escucho a cada rato(no se pudo hacer el cliente de http)");

        while running.load(Ordering::SeqCst) {
            match cap.next_packet() {
                Ok(packet) => {
                    if let Some(dns_request) = parse_dns_packet(&packet.data) {
                        if dns_request.header.query {
                            let mut stats = stats.lock().unwrap();
                            stats.increment_total();
                            stats.add_request_by_domain(dns_request.query_name.clone());
                            drop(stats);

                            let siono_estandar =
                                dns_request.header.opcode == Opcode::StandardQuery;

                            if siono_estandar {
                                println!("llego un estandar, vealo {}", dns_request);

                                let la_roomba = dns_request.query_name.trim_end_matches('.');
                                let body = json!({ "domain": la_roomba });

                                match el_http
                                    .post("http://localhost:8080/api/exists")
                                    .json(&body)
                                    .send()
                                {
                                    Ok(resp) if resp.status().is_success() => {
                                        let json_resp: serde_json::Value = resp
                                            .json()
                                            .unwrap_or(serde_json::Value::Null);
                                        println!("Respuesta de la API: {:?}", json_resp);
                                    }
                                    Ok(resp) => {
                                        eprintln!(
                                            " el exists dio el nombre de: {}",
                                            resp.status()
                                        );
                                    }
                                    Err(e) => {
                                        eprintln!("implosiono asi que....ni modo dio esto {}",e);
                                    }
                                }
                            } else {
                                println!("No estandar llego {}", dns_request);

                                let secreto_de_estado = general_purpose::STANDARD
                                    .encode(&dns_request.raw_payload);

                                let body = json!({ "data": secreto_de_estado });

                                match el_http
                                    .post("http://localhost:8080/api/dns_resolver")
                                    .json(&body)
                                    .send()
                                {
                                    Ok(resp) if resp.status().is_success() => {
                                        match resp.json::<serde_json::Value>() {
                                            Ok(v) => {
                                                if let Some(mensaje) =
                                                    v.get("data").and_then(|x| x.as_str())
                                                {
                                                    match general_purpose::STANDARD
                                                        .decode(mensaje)
                                                    {
                                                        Ok(dns_bytes) => {
                                                            println!(
                                                                " {} de bytes de vuelta",
                                                                dns_bytes.len()
                                                            );
                                                        }
                                                        Err(e) => eprintln!(
                                                            "Error de descodificado, el error es: {}",
                                                            e
                                                        ),
                                                    }
                                                } else {
                                                    println!(" Respuesta de la API: {}", v)
                                                }
                                            }
                                            Err(e) => {
                                                eprintln!("Error de parsear el JSON: {}", e)
                                            }
                                        }
                                    }
                                    Ok(resp) => {
                                        eprintln!(
                                            "  el dns_resolver devolvio el HTTP {}", resp.status()                                        );
                                    }
                                    Err(e) => {
                                        eprintln!(
                                            " Error con el resolver, lo siento: {}",
                                            e
                                        );
                                    }
                                }
                            }
                        }
                    }
                }
                Err(e) => {
                    eprintln!("Error con el pack-ete: {}", e);
                    break;
                }
            }
        }

        if let Err(e) = stats_thread.join() {
            eprintln!("Error con las stats. {:?}", e);
        }

        let stats = stats.lock().unwrap();
        print_final_stats(&stats);
        println!("Ya ya, detenido el arroz.");
    }

    fn parse_dns_packet(packet: &[u8]) -> Option<DnsRequest> {
        if packet.len() < 42 {
            return None;
        }

        if packet[12] != 0x08 || packet[13] != 0x00 {
            return None;
        }

        let ip_start = 14;

        let ihl = ((packet[ip_start] & 0x0f) as usize) * 4;
        if ihl < 20 {
            return None;
        }

        if packet[ip_start + 9] != 17 {
            return None;
        }

        let src_ip = std::net::Ipv4Addr::new(
            packet[ip_start + 12],
            packet[ip_start + 13],
            packet[ip_start + 14],
            packet[ip_start + 15],
        );

        let udp_start = ip_start + ihl;
        if packet.len() < udp_start + 8 {
            return None;
        }

        let src_port = u16::from_be_bytes([packet[udp_start], packet[udp_start + 1]]);

        let dns_start = udp_start + 8;
        let dns_payload = &packet[dns_start..];

        let dns_packet = DnsPacket::parse(dns_payload).ok()?;

        if !dns_packet.header.query {
            return None;
        }

        let question = dns_packet.questions.first()?;
        let query_name = question.qname.to_string();
        let qtype = question.qtype;
        let qclass = question.qclass;

        Some(DnsRequest {
            query_name,
            source_ip: src_ip.into(),
            source_port: src_port,
            qtype,
            qclass,
            header: dns_packet.header,
            raw_payload: dns_payload.to_vec(),
        })
    }
}

fn main() {
    dns_interceptor::main();
}