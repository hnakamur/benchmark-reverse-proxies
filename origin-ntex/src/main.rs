use std::io;

use ntex::http::header::{HeaderValue, CONTENT_TYPE, SERVER};
use ntex::http::{self, HttpService, Response};
use ntex::util::PoolId;
use ntex::{time::Seconds, util::Ready};

static TEXT_PLAIN: HeaderValue = HeaderValue::from_static("text/plain");
static NTEX: HeaderValue = HeaderValue::from_static("ntex");

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
                .disconnect_timeout(Seconds(1))
                .finish(|_req| {
                    let mut res = Response::Ok();
                    res.header(CONTENT_TYPE, &TEXT_PLAIN);
                    res.header(SERVER, &NTEX);
                    Ready::Ok::<_, io::Error>(res.body("Hello, world!\n"))
                })
        })?
        .run()
        .await
}
