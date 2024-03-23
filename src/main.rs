use std::{
    env,
    error::Error,
    fs::{create_dir_all, File},
    io::Write,
    path::PathBuf,
    process::Command,
    thread,
    time::Duration,
};

fn main() {
    bench_http_server("hello-actix-http-server").unwrap();
    bench_http_server("hello-hyper-server").unwrap();
    bench_http_server("hello-pingora-server").unwrap();
    bench_http_server("nginx").unwrap();
}

pub type DynError = Box<dyn Error + Send + Sync + 'static>;

fn bench_http_server(name: &str) -> Result<(), DynError> {
    let mut server = if name == "nginx" {
        let mut path = env::current_dir()?;
        path.push("config/nginx.conf");
        let path = path.into_os_string().into_string().unwrap();
        Command::new("/usr/sbin/nginx")
            .args(["-c", &path, "-g", "daemon off;"])
            .spawn()?
    } else {
        let mut server_path = PathBuf::from("target/release");
        server_path.push(name);
        Command::new(server_path).spawn()?
    };

    let mut dir = PathBuf::from("results");
    dir.push(name);
    create_dir_all(&dir)?;

    thread::sleep(Duration::from_secs(2));
    let output = Command::new("curl")
        .args(["-sSD", "-", "http://localhost:3000"])
        .output()?;
    let mut path = dir.clone();
    path.push("curl.txt");
    let mut file = File::create(path)?;
    file.write_all(&output.stdout)?;

    thread::sleep(Duration::from_secs(1));
    let output = Command::new("oha")
        .args([
            "--no-tui",
            "--json",
            "-c",
            "100",
            "-z",
            "15s",
            "--latency-correction",
            "--disable-keepalive",
            "http://localhost:3000",
        ])
        .output()?;
    let mut path = dir.clone();
    path.push("oha.json");
    let mut file = File::create(path)?;
    file.write_all(&output.stdout)?;

    server.kill()?;
    let output = server.wait_with_output()?;
    let mut path = dir.clone();
    path.push("server.txt");
    let mut file = File::create(path)?;
    file.write_all(&output.stdout)?;
    Ok(())
}
