use std::io;

use ntex::http::header::{HeaderValue, CONTENT_TYPE, SERVER};
use ntex::http::{HttpService, Response};
use ntex::{time::Seconds, util::Ready};

static TEXT_PLAIN: HeaderValue = HeaderValue::from_static("text/plain");
static NTEX: HeaderValue = HeaderValue::from_static("ntex");

#[ntex::main]
async fn main() -> io::Result<()> {
    env_logger::init();

    ntex::server::build()
        .bind("hello-world", "127.0.0.1:3000", |_| {
            HttpService::build()
                .headers_read_rate(Seconds(1), Seconds(3), 128)
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
