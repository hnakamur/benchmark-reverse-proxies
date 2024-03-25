use std::{sync::Arc, time::SystemTime};

use async_trait::async_trait;
use chrono::DateTime;
use pingora::{
    apps::ServerApp,
    protocols::Stream,
    server::{Server, ShutdownWatch},
    services::{listening::Service as ListeningService, Service},
};
use tokio::io::AsyncWriteExt;

#[derive(Clone)]
pub struct HelloApp;

const BODY: &[u8] = b"Hello, world!\n";

fn to_date_string(epoch_sec: i64) -> String {
    let dt = DateTime::from_timestamp(epoch_sec, 0).unwrap();
    dt.format("%a, %d %b %Y %H:%M:%S GMT").to_string()
}

#[async_trait]
impl ServerApp for HelloApp {
    async fn process_new(
        self: &Arc<Self>,
        mut io: Stream,
        _shutdown: &ShutdownWatch,
    ) -> Option<Stream> {
        let d = SystemTime::now()
            .duration_since(SystemTime::UNIX_EPOCH)
            .unwrap();
        let date = to_date_string(d.as_secs() as i64);
        let header = format!("HTTP/1.1 200 OK\r\ncontent-type: text/plain\r\ncontent-length: {}\r\ndate: {}\r\nserver: pingora\r\n\r\n", BODY.len(), date);
        io.write_all(header.as_bytes()).await.unwrap();
        io.write_all(BODY).await.unwrap();
        io.flush().await.unwrap();
        None
    }
}

impl HelloApp {
    pub fn new() -> Arc<Self> {
        Arc::new(HelloApp {})
    }
}

pub fn hello_service() -> ListeningService<HelloApp> {
    ListeningService::new("Hello Service HTTP".to_string(), HelloApp::new())
}

fn main() {
    let mut my_server = Server::new(None).unwrap();
    my_server.bootstrap();

    let mut hello_service = hello_service();
    hello_service.add_tcp("0.0.0.0:3000");

    let services: Vec<Box<dyn Service>> = vec![Box::new(hello_service)];
    my_server.add_services(services);
    my_server.run_forever();
}
