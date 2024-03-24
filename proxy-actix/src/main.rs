use std::{convert::Infallible, io};

use actix_http::{HttpService, Request, Response, StatusCode, Uri};
use actix_server::Server;
use awc::Client;

#[actix_rt::main]
async fn main() -> io::Result<()> {
    env_logger::init_from_env(env_logger::Env::new().default_filter_or("error"));

    Server::build()
        .bind("hello-world", ("127.0.0.1", 3001), || {
            HttpService::build()
                .finish(|req: Request| async move {
                    let mut req = req;
                    *req.uri_mut() = "http://localhost:3000".parse::<Uri>().unwrap();
                    let mut client = Client::default();
                    let res = client.request_from(req.head()).send().await.unwrap();
                    res.insert_header(("Server", "actix"));
                    Ok::<_, Infallible>(res)
                })
                .tcp()
        })?
        .run()
        .await
}
