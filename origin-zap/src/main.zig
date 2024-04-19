const std = @import("std");
const zap = @import("zap");

pub const log_level: std.log.Level = .warn;

fn on_request_verbose(r: zap.Request) void {
    if (r.path) |the_path| {
        std.debug.print("PATH: {s}\n", .{the_path});
    }

    if (r.query) |the_query| {
        std.debug.print("QUERY: {s}\n", .{the_query});
    }
    r.sendBody("<html><body><h1>Hello from ZAP!!!</h1></body></html>") catch return;
}

fn on_request_minimal(r: zap.Request) void {
    r.setHeader("server", "origin-zap") catch |err| {
        std.debug.print("setHeader failed: {s}\n", .{@errorName(err)});
    };
    r.setContentType(.TEXT) catch |err| {
        std.debug.print("setContentType failed: {s}\n", .{@errorName(err)});
    };
    r.sendBody("Hello world!\n") catch return;
}

pub fn main() !void {
    var listener = zap.HttpListener.init(.{
        .port = 3000,
        .on_request = on_request_minimal,
        .log = true,
        .max_clients = 100000,
    });
    try listener.listen();

    std.debug.print("Listening on 0.0.0.0:3000\n", .{});

    // start worker threads
    zap.start(.{
        .threads = 24,
        .workers = 24,
    });
}
