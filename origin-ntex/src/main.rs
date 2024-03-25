use std::io;

use ntex::http::header::{HeaderValue, CONTENT_TYPE, SERVER};
use ntex::http::{self, HttpService, Response};
use ntex::util::{Bytes, PoolId};
use ntex::{time::Seconds, util::Ready};

const TEXT_PLAIN: HeaderValue = HeaderValue::from_static("text/plain");
const NTEX: HeaderValue = HeaderValue::from_static("ntex");
const BODY_TEXT: Bytes = Bytes::from_static(b"Hello, world!\n");

#[ntex::main]
async fn main() -> io::Result<()> {
    env_logger::init();

    ntex::server::build()
        .backlog(1024)
        .bind("hello-world", "127.0.0.1:3000", |cfg| {
            cfg.memory_pool(PoolId::P1);
            PoolId::P1.set_read_params(65535, 2048);
            PoolId::P1.set_write_params(65535, 2048);

            HttpService::build()
                .keep_alive(http::KeepAlive::Os)
                .client_timeout(Seconds::ZERO)
                .headers_read_rate(Seconds::ZERO, Seconds::ZERO, 0)
                .payload_read_rate(Seconds::ZERO, Seconds::ZERO, 0)
                .finish(|_req| {
                    let mut res = Response::with_body(
                        http::StatusCode::OK,
                        http::body::Body::Bytes(BODY_TEXT),
                    );
                    let headers = res.headers_mut();
                    headers.insert(CONTENT_TYPE, TEXT_PLAIN);
                    headers.insert(SERVER, NTEX);
                    Ready::Ok::<_, io::Error>(res)
                })
        })?
        .run()
        .await
}
