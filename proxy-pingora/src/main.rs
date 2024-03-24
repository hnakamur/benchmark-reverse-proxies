use std::net::{IpAddr, Ipv4Addr, SocketAddr};

use async_trait::async_trait;
use pingora::{http::ResponseHeader, server::Server, services::Service, upstreams::peer::HttpPeer};
use pingora_core::Result;
use pingora_proxy::{ProxyHttp, Session};

struct MyProxy;

#[async_trait]
impl ProxyHttp for MyProxy {
    type CTX = ();
    fn new_ctx(&self) -> Self::CTX {}

    async fn upstream_peer(&self, _session: &mut Session, _ctx: &mut ()) -> Result<Box<HttpPeer>> {
        let upstream = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)), 3000);
        let peer = Box::new(HttpPeer::new(upstream, false, "".to_string()));
        Ok(peer)
    }

    async fn response_filter(
        &self,
        _session: &mut Session,
        upstream_response: &mut ResponseHeader,
        _ctx: &mut Self::CTX,
    ) -> Result<()> {
        upstream_response.insert_header("Server", "pingora")
    }
}

fn main() {
    let mut my_server = Server::new(None).unwrap();
    my_server.bootstrap();

    let mut echo_service_http =
        pingora_proxy::http_proxy_service(&my_server.configuration, MyProxy);
    echo_service_http.add_tcp("0.0.0.0:3001");

    let services: Vec<Box<dyn Service>> = vec![Box::new(echo_service_http)];
    my_server.add_services(services);
    my_server.run_forever();
}
