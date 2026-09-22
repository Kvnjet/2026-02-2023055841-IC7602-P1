mod dns_api;

pub mod dns_interceptor {

    use pcap::{Capture};
    use dns_parser::{Packet as DnsPacket, Opcode};
    use std::collections::HashMap;
    use std::net::IpAddr;
    use std::sync::atomic::{AtomicBool, Ordering};
    use std::sync::{Arc, Mutex};
    use std::{println, thread};
    use std::time::{Duration};

    // Estructura del DNS
    #[derive(Debug, Clone)]
    pub struct DnsRequest {
        pub query_name: String,
        pub source_ip: IpAddr,
        pub source_port: u16,
        pub qtype: dns_parser::QueryType,
        pub qclass: dns_parser::QueryClass,
        pub header: dns_parser::Header,
        pub raw_payload: Vec<u8>, // Todo lo del DNS para que la API se pegue con el

    }

    //Cuerpo facil para formatear el output

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
                if h.query { 0 } else { 1 }, //Es el QR, si aca de 0 es query a lo que entendi, si no es respuesta
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

    // Estructura para guardar las estadisticas del interceptor
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

        // Methods to get stats for API
        pub fn get_total_requests(&self) -> usize {
            self.total_requests
        }

        pub fn get_requests_by_domain(&self) -> &HashMap<String, usize> {
            &self.requests_by_domain
        }
    }

    // Pasa los argumentos
    #[derive(Debug)]
    pub struct Args {
        pub debug: bool,
        pub api_port: u16,
    }

    impl Args {
        pub fn parse() -> Self {
            let mut debug = false;
            let mut api_port = 9090; // Puerto default

            // Revisa argumentos
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

    // Como quedo(para traquear si va o no)
    fn print_final_stats(stats: &Stats) {
        println!("\n--- Final DNS Statistics ---");
        println!("Total requests: {}", stats.total_requests);

        // Print los dominios
        let mut domains: Vec<_> = stats.requests_by_domain.iter().collect();
        domains.sort_by(|a, b| b.1.cmp(a.1));
        println!("Top domains:");
        for (domain, count) in domains.iter().take(5) {
            println!("  {}: {} requests", domain, count);
        }
    }

    // El API del interceptor aca ayuda
    pub fn start_api_server(stats: Arc<Mutex<Stats>>, port: u16) {
        // Filtro de la API del interceptor
        let api_filter = crate::dns_api::api_filter(stats);

        // Hilo del interceptor
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
        
        // Objeto para la interfaz
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

        // Sin esto le cuesta cerrarse
        ctrlc::set_handler(move || {
            println!(" Ya ya ya se esta cerrando...");
            r.store(false, Ordering::SeqCst);
        }).expect("Imposible de cerrar, lo siento, es mas sudo que yo");


        let stats = Arc::new(Mutex::new(Stats::new()));
        let stats_for_thread = Arc::clone(&stats);

        // Print para saber que sirve
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
                
                // Print top domains
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

        // Procesa los paquetes
        while running.load(Ordering::SeqCst) {
            match cap.next_packet() {
                Ok(packet) => {

                    if let Some(dns_request) = parse_dns_packet(&packet.data) {
                        if dns_request.header.query {
                            //updatea
                            let mut stats = stats.lock().unwrap();
                            stats.increment_total();
                            stats.add_request_by_domain(dns_request.query_name.clone());
                            drop(stats);   //si amas algo dejalo ir o algo asi

                            // Classify the query
                            let is_standard =
                                dns_request.header.opcode == Opcode::StandardQuery;
                            if is_standard{
                                println!("Standar {}", dns_request);
                                // Mae aca falta lo de la API pero no se puede aun, tlabajen.
                            }
                            else{
                                println!("No standar {}", dns_request);

                                use base64::{engine::general_purpose, Engine as _};
                                let codificado = general_purpose::STANDARD.encode(&dns_request.raw_payload);
                                println!{
                                    "Aca pondria mi post SI TUVIERA MI API PARA HACERLE POST {}", codificado.len(), "bytes"
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
    // Minimum: Ethernet (14) + IPv4 (20) + UDP (8) = 42 bytes
        if packet.len() < 42 {
            return None;
        }

        // Revisa que sea ipv4
        if packet[12] != 0x08 || packet[13] != 0x00 {
            return None;
        }

        let ip_start = 14;

        // IPv4 length del header
        let ihl = ((packet[ip_start] & 0x0f) as usize) * 4;
        if ihl < 20 {
            return None;
        }

        // El protocolo de UDP que sea 17
        if packet[ip_start + 9] != 17 {
            return None;
        }

        // LA IP de donde viene
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

        //Filtro para que solo agarre queries, respuestas no importa
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