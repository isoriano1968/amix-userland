# mesa-demos for AMIX

The demos on the Mesa 18 package, installed in `/opt/mesa-demos`; run them with
`mesa-demo`.  `make build stage`, with `SETS="glsl demos trivial tests redbook samples xdemos"` for all of them. The programs share libmesademo.so.1 in /opt/mesa-demos/lib.

The sets share some names: the first set in `SETS` keeps the name, others get the set in front, such as `trivial-tri`.
