target/release/origin-c-epoll: main.c
	mkdir -p target/release
	cc -O3 -o $@ $<

format:
	clang-format -i main.c

clean:
	rm -r target

.PHONY: format clean
