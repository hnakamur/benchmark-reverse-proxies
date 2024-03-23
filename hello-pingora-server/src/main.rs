use std::sync::Arc;

use async_trait::async_trait;
use bytes::Bytes;
use http::{Response, StatusCode};
use pingora::{
    apps::http_app::ServeHttp,
    protocols::http::ServerSession,
    server::Server,
    services::{listening::Service as ListeningService, Service},
};

pub struct HttpHelloApp;

#[async_trait]
impl ServeHttp for HttpHelloApp {
    async fn response(&self, _http_stream: &mut ServerSession) -> Response<Vec<u8>> {
        let body = Bytes::from("Hello, world!\n");

        Response::builder()
            .status(StatusCode::OK)
            .header(http::header::SERVER, "pingora")
            .header(http::header::CONTENT_TYPE, "text/plain")
            .header(http::header::CONTENT_LENGTH, body.len())
            .body(body.to_vec())
            .unwrap()
    }
}

pub fn new_http_hello_app() -> Arc<HttpHelloApp> {
    Arc::new(HttpHelloApp {})
}

pub fn hello_service_http() -> ListeningService<HttpHelloApp> {
    ListeningService::new("Hello Service HTTP".to_string(), new_http_hello_app())
}

fn main() {
    let mut my_server = Server::new(None).unwrap();
    my_server.bootstrap();

    let mut echo_service_http = hello_service_http();
    echo_service_http.add_tcp("0.0.0.0:3000");

    let services: Vec<Box<dyn Service>> = vec![Box::new(echo_service_http)];
    my_server.add_services(services);
    my_server.run_forever();
}
