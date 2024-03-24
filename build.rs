use std::process::Command;

fn main() {
    let dirs = [
        "origin-actix",
        "origin-hyper",
        "origin-pingora",
        "proxy-actix",
        "proxy-hyper",
        "proxy-pingora",
    ];
    for dir in dirs {
        // println!("cargo::rerun-if-changed={}/src/main.rs", dir);
        let mut child = Command::new("cargo")
            .args(["build", "--release"])
            .current_dir(dir)
            .spawn()
            .unwrap();
        child.wait().unwrap();
    }
}
