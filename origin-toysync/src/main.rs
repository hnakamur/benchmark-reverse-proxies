use std::io::{BufRead, BufReader, Write};
use std::net::{TcpListener, TcpStream};
use std::time::SystemTime;

fn main() {
    let listener = TcpListener::bind("127.0.0.1:3000").unwrap();

    for stream in listener.incoming() {
        let stream = stream.unwrap();

        handle_connection(stream);
    }
}

fn handle_connection(mut stream: TcpStream) {
    let buf_reader = BufReader::new(&mut stream);
    let _http_request: Vec<_> = buf_reader
        .lines()
        .map(|result| result.unwrap())
        .take_while(|line| !line.is_empty())
        .collect();

    let now = SystemTime::now();
    let date = format!("{}", httpdate::HttpDate::from(now));
    let content = "Hello, world!\n";
    let response = format!("HTTP/1.1 200 OK\r\nServer: toysync\r\nDate: {}\r\nContent-Type: text/plain\r\nContent-Length: {}\r\n\r\n{}", date, content.len(), content);

    stream.write_all(response.as_bytes()).unwrap();
}
