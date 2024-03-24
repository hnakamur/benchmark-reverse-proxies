use std::convert::Infallible;
use std::net::SocketAddr;

use hyper::header::{HeaderValue, SERVER};
use hyper::server::conn::http1;
use hyper::service::service_fn;
use hyper::{Request, Response};
use hyper_util::rt::TokioIo;
use once_cell::sync::Lazy;
use reqwest::Body;
use tokio::net::TcpListener;

static CLIENT: Lazy<reqwest::Client> = Lazy::new(reqwest::Client::new);

async fn hello(_client_req: Request<hyper::body::Incoming>) -> Result<Response<Body>, Infallible> {
    // TODO: Set url path and request headers
    let res = CLIENT.get("http://localhost:3000").send().await.unwrap();
    let mut res: http::Response<Body> = res.into();
    let headers = res.headers_mut();
    headers.insert(SERVER, HeaderValue::from_static("hyper"));
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
