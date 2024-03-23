use std::{convert::Infallible, io};

use actix_http::{HttpService, Request, Response, StatusCode};
use actix_server::Server;

#[actix_rt::main]
async fn main() -> io::Result<()> {
    env_logger::init_from_env(env_logger::Env::new().default_filter_or("error"));

    Server::build()
        .bind("hello-world", ("127.0.0.1", 3000), || {
            HttpService::build()
                .finish(|_req: Request| async move {
                    let mut res = Response::build(StatusCode::OK);
                    res.insert_header(("Server", "actix"));
                    res.insert_header(("Content-Type", "text/plain"));
                    Ok::<_, Infallible>(res.body("Hello, world!\n"))
                })
                .tcp()
        })?
        .run()
        .await
}
