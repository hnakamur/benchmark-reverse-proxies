use std::io;

use ntex::http::header::{HeaderValue, CONTENT_TYPE, SERVER};
use ntex::http::{self, HttpService, HttpServiceConfig, Response};
use ntex::time::Seconds;
use ntex::util::{Bytes, Ready};
use ntex::SharedCfg;

const TEXT_PLAIN: HeaderValue = HeaderValue::from_static("text/plain");
const NTEX: HeaderValue = HeaderValue::from_static("ntex");
const BODY_TEXT: Bytes = Bytes::from_static(b"Hello, world!\n");

#[ntex::main]
async fn main() -> io::Result<()> {
    env_logger::init();

    ntex::server::build()
        .backlog(1024)
        .bind("hello-world", "127.0.0.1:3000", async |_| {
            HttpService::new(|_req| {
                let mut res = Response::with_body(
                    http::StatusCode::OK,
                    BODY_TEXT,
                );
                let headers = res.headers_mut();
                headers.insert(CONTENT_TYPE, TEXT_PLAIN);
                headers.insert(SERVER, NTEX);
                Ready::Ok::<_, io::Error>(res)
            })
        })?
        .config(
            "hello-world",
            SharedCfg::new("HELLO-WORLD").add(
                HttpServiceConfig::new()
                    .set_keepalive(http::KeepAlive::Os)
                    .set_client_timeout(Seconds::ZERO)
                    .set_headers_read_rate(Seconds::ZERO, Seconds::ZERO, 0)
                    .set_payload_read_rate(Seconds::ZERO, Seconds::ZERO, 0),
            ),
        )
        .run()
        .await
}
