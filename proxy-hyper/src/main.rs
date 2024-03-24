use std::net::SocketAddr;

use http::{Request, Response, Uri};
use hyper::body::Incoming;
use hyper::client::conn;
use hyper::header::{HeaderValue, SERVER};
use hyper::server::conn::http1;
use hyper::service::service_fn;
use hyper_util::rt::TokioIo;
use tokio::net::{TcpListener, TcpStream};

async fn hello(
    req: Request<Incoming>,
) -> Result<Response<Incoming>, Box<dyn std::error::Error + Send + Sync>> {
    let mut req = req;
    *req.uri_mut() = "http://localhost:3000".parse::<Uri>().unwrap();
    let stream = TcpStream::connect(req.uri().authority().unwrap().as_str()).await?;
    let io = TokioIo::new(stream);
    let (mut sender, conn) = conn::http1::handshake(io).await?;
    tokio::task::spawn(async move {
        if let Err(err) = conn.await {
            println!("Connection failed: {:?}", err);
        }
    });
    let mut res = sender.send_request(req).await?;
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
