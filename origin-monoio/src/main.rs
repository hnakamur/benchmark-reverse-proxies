use bytes::Bytes;
use chrono::Utc;
use http::{response::Builder, StatusCode};
use monoio::{
    io::{
        sink::{Sink, SinkExt},
        stream::Stream,
        Splitable,
    },
    net::{TcpListener, TcpStream},
};
use monoio_http::{
    common::{error::HttpError, request::Request, response::Response},
    h1::{
        codec::{decoder::RequestDecoder, encoder::GenericEncoder},
        payload::{FixedPayload, Payload},
    },
    util::spsc::{spsc_pair, SPSCReceiver},
};

#[monoio::main]
async fn main() {
    let listener = TcpListener::bind("127.0.0.1:3000").unwrap();
    // println!("Listening");
    loop {
        let incoming = listener.accept().await;
        match incoming {
            Ok((stream, _addr)) => {
                // println!("accepted a connection from {}", addr);
                monoio::spawn(handle_connection(stream));
            }
            Err(e) => {
                println!("accepted connection failed: {}", e);
            }
        }
    }
}

async fn handle_connection(stream: TcpStream) {
    let (r, w) = stream.into_split();
    let sender = GenericEncoder::new(w);
    let mut receiver = RequestDecoder::new(r);
    let (mut tx, rx) = spsc_pair();
    monoio::spawn(handle_task(rx, sender));

    loop {
        match receiver.next().await {
            None => {
                // println!("connection closed, connection handler exit");
                return;
            }
            Some(Err(_)) => {
                println!("receive request failed, connection handler exit");
                return;
            }
            Some(Ok(item)) => match tx.send(item).await {
                Err(_) => {
                    println!("request handler dropped, connection handler exit");
                    return;
                }
                Ok(_) => {
                    // println!("request handled success");
                }
            },
        }
    }
}

async fn handle_task(
    mut receiver: SPSCReceiver<Request>,
    mut sender: impl Sink<Response, Error = impl Into<HttpError>>,
) -> Result<(), HttpError> {
    loop {
        let request = match receiver.recv().await {
            Some(r) => r,
            None => {
                return Ok(());
            }
        };
        let resp = handle_request(request).await;
        sender.send_and_flush(resp).await.map_err(Into::into)?;
    }
}

async fn handle_request(_req: Request) -> Response {
    let now = Utc::now();
    let date = now.format("%a, %d %b %Y %H:%M:%S GMT").to_string();
    Builder::new()
        .status(StatusCode::OK)
        .header("Server", "monoio")
        .header("Content-Type", "text/plain")
        .header("Date", date)
        .body(Payload::Fixed(FixedPayload::new(Bytes::from(
            "Hello, world!\n",
        ))))
        .unwrap()
}
