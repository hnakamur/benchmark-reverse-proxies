use std::net::SocketAddr;

use http::header::SERVER;
use http::{HeaderValue, Request as HttpRequest, Response as HttpResponse, Uri};
use hyper::body::Incoming;
use hyper::server::conn::http1;
use hyper::service::service_fn;
use hyper_util::client::legacy::{connect::HttpConnector, Client};
use hyper_util::rt::TokioIo;
use once_cell::sync::Lazy;
use tokio::net::TcpListener;

static CLIENT: Lazy<Client<HttpConnector, Incoming>> =
    Lazy::new(|| Client::builder(hyper_util::rt::TokioExecutor::new()).build_http());
static HYPER: HeaderValue = HeaderValue::from_static("hyper");

async fn hello(
    req: HttpRequest<Incoming>,
) -> Result<HttpResponse<Incoming>, Box<dyn std::error::Error + Send + Sync>> {
    let mut req = req;
    *req.uri_mut() = "http://localhost:3000".parse::<Uri>().unwrap();
    let client = CLIENT.clone();
    let mut res = client.request(req).await?;
    let headers = res.headers_mut();
    headers.insert(SERVER, HYPER.clone());
    Ok(res)
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let addr = SocketAddr::from(([127, 0, 0, 1], 3001));
    let listener = TcpListener::bind(addr).await?;
    loop {
        let (stream, _) = listener.accept().await?;
        let io = TokioIo::new(stream);
        tokio::task::spawn(async move {
            if let Err(err) = http1::Builder::new()
                .serve_connection(io, service_fn(hello))
                .await
            {
                println!("Error serving connection: {:?}", err);
            }
        });
    }
}
