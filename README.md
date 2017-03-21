# Stampede benchmarks

Docker-based build provided for portability and convenience. Actual benchmark
runs should run be natively and with huge pages enabled. Performance results
available in [nguyen17].

[nguyen17] Donald Nguyen and Keshav Pingali. "What Scalable Programs Need from
Transactional Memory." In ASPLOS 2017.
[preprint](https://github.com/ddn0/stampede/blob/master/docs/nguyen17.pdf)

Basic configuration:

```bash
docker build -t stampede .
docker run -v "$(pwd)":/src stampede /src/scripts/download-contrib
docker run -v "$(pwd)":/src -v "$(pwd)/build":/build stampede /src/scripts/configure
docker run -v "$(pwd)":/src -v "$(pwd)/build":/build stampede /src/scripts/build
docker run -v "$(pwd)":/src -v "$(pwd)/build":/build stampede /src/scripts/run-suite --input-size=small
```

(native builds would omit `docker ... stampede`)

See [configure](https://github.com/ddn0/stampede/blob/master/scripts/configure) 
and [run-suite](https://github.com/ddn0/stampede/blob/master/scripts/run-suite)
for more details.
