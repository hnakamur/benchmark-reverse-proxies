use std::io::{BufRead, BufReader, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::{Arc, Mutex};
use std::thread;

mod date;

fn main() {
    let listener = TcpListener::bind("127.0.0.1:3000").unwrap();
    let listener = Arc::new(Mutex::new(listener));
    let count = thread::available_parallelism().unwrap().get();

    let mut handles = vec![];

    for _ in 0..count {
        let listener_clone = Arc::clone(&listener);
        let handle = thread::spawn(move || loop {
            let listener = listener_clone.lock().unwrap();
            match listener.accept() {
                Ok((stream, _addr)) => {
                    handle_connection(stream);
                }
                Err(e) => {
                    eprintln!("Failed to accept client: {}", e);
                    break;
                }
            }
        });
        handles.push(handle);
    }

    for handle in handles {
        handle.join().unwrap();
    }
}

fn handle_connection(mut stream: TcpStream) {
    let mut buf_reader = BufReader::new(&mut stream);
    let _buffer = buf_reader.fill_buf().unwrap();

    let content = "Hello, world!\n";
    let response = format!("HTTP/1.1 200 OK\r\nServer: toysync\r\nDate: {}\r\nContent-Type: text/plain\r\nContent-Length: {}\r\n\r\n{}", date::now(), content.len(), content);
    stream.write_all(response.as_bytes()).unwrap();
}
