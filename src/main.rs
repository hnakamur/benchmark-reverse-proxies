use log::info;
use nix::{
    sys::signal::{kill, Signal::SIGTERM},
    unistd::Pid,
};
use std::{
    env,
    error::Error,
    fs::{create_dir_all, File},
    io::Write,
    path::{Path, PathBuf},
    process::{Child, Command},
    thread,
    time::Duration,
};

fn main() {
    env_logger::init_from_env(env_logger::Env::new().default_filter_or("info"));

    bench_http_origin(Server::Rust(String::from("origin-actix"))).unwrap();
    bench_http_origin(Server::Rust(String::from("origin-hyper"))).unwrap();
    bench_http_origin(Server::Rust(String::from("origin-pingora"))).unwrap();
    bench_http_origin(Server::Nginx(String::from("origin-nginx"))).unwrap();

    bench_http_proxy(
        Server::Rust(String::from("proxy-hyper")),
        Server::Nginx(String::from("origin-nginx")),
    )
    .unwrap();
    bench_http_proxy(
        Server::Rust(String::from("proxy-pingora")),
        Server::Nginx(String::from("origin-nginx")),
    )
    .unwrap();
    bench_http_proxy(
        Server::Nginx(String::from("proxy-nginx")),
        Server::Nginx(String::from("origin-nginx")),
    )
    .unwrap();
}

pub type DynError = Box<dyn Error + Send + Sync + 'static>;

fn bench_http_origin(origin: Server) -> Result<(), DynError> {
    let name = origin.name();
    info!("benchmark origin: {}...", name);
    let mut origin_proc = origin.spawn()?;

    let mut dir = PathBuf::from("results");
    dir.push(name);
    create_dir_all(&dir)?;

    let url = "http://localhost:3000";

    thread::sleep(Duration::from_secs(2));
    run_curl(url, &dir)?;

    thread::sleep(Duration::from_secs(1));
    run_oha(url, &dir)?;

    origin.kill(&mut origin_proc)?;
    wait_and_write_output(origin_proc, &dir, "origin.txt")?;
    Ok(())
}

fn bench_http_proxy(proxy: Server, origin: Server) -> Result<(), DynError> {
    let name = proxy.name();
    info!("benchmark proxy: {}, origin: {}...", name, origin.name());
    let mut origin_proc = origin.spawn()?;
    let mut proxy_proc = proxy.spawn()?;

    let mut dir = PathBuf::from("results");
    dir.push(name);
    create_dir_all(&dir)?;

    let url = "http://localhost:3001";

    thread::sleep(Duration::from_secs(2));
    run_curl(url, &dir)?;

    thread::sleep(Duration::from_secs(1));
    run_oha(url, &dir)?;

    proxy.kill(&mut proxy_proc)?;
    wait_and_write_output(proxy_proc, &dir, "proxy.txt")?;
    origin.kill(&mut origin_proc)?;
    wait_and_write_output(origin_proc, &dir, "origin.txt")?;
    Ok(())
}

enum Server {
    Rust(String),
    Nginx(String),
}

impl Server {
    fn name(&self) -> String {
        match self {
            Server::Rust(name) => name.clone(),
            Server::Nginx(config_dir) => config_dir.clone(),
        }
    }

    fn spawn(&self) -> Result<Child, DynError> {
        match self {
            Server::Rust(name) => {
                let mut server_path = PathBuf::from(name);
                server_path.push("target/release");
                server_path.push(name);
                Ok(Command::new(server_path).spawn()?)
            }
            Server::Nginx(config_dir) => {
                let mut path = env::current_dir()?;
                path.push(config_dir);
                path.push("nginx.conf");
                let path = path.into_os_string().into_string().unwrap();
                Ok(Command::new("/usr/sbin/nginx")
                    .args(["-c", &path, "-g", "daemon off;"])
                    .spawn()?)
            }
        }
    }

    fn kill(&self, proc: &mut Child) -> Result<(), DynError> {
        match self {
            Server::Rust(_) => proc.kill()?,
            Server::Nginx(_) => kill(Pid::from_raw(proc.id() as i32), SIGTERM)?,
        }
        Ok(())
    }
}

fn run_curl<P: AsRef<Path>>(url: &str, output_dir: P) -> Result<(), DynError> {
    let output = Command::new("curl").args(["-sSD", "-", url]).output()?;
    let mut path = PathBuf::from(output_dir.as_ref());
    path.push("curl.txt");
    let mut file = File::create(path)?;
    file.write_all(&output.stdout)?;
    Ok(())
}

fn run_oha<P: AsRef<Path>>(url: &str, output_dir: P) -> Result<(), DynError> {
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
            url,
        ])
        .output()?;
    let mut path = PathBuf::from(output_dir.as_ref());
    path.push("oha.json");
    let mut file = File::create(path)?;
    file.write_all(&output.stdout)?;
    Ok(())
}

fn wait_and_write_output<P: AsRef<Path>, P2: AsRef<Path>>(
    proc: Child,
    output_dir: P,
    filename: P2,
) -> Result<(), DynError> {
    let output = proc.wait_with_output()?;
    let mut path = PathBuf::from(output_dir.as_ref());
    path.push(filename.as_ref());
    let mut file = File::create(path)?;
    file.write_all(&output.stdout)?;
    Ok(())
}
