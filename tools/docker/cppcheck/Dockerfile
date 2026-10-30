###### BUILD IMAGE
FROM alpine:3.24@sha256:be2656061b536c1883c4034790e21d0a09c5aaaf51968b89368a3c0371ea2c9a AS build

# Several installs
RUN apk update
RUN apk add git=2.54.0-r0 cmake=4.2.3-r0 ninja-build=1.13.2-r1 build-base=0.5-r4 python3=3.14.10-r1 make=4.4.1-r4

# cppcheck
WORKDIR /build
RUN git clone https://github.com/RomainPelletant/cppcheck
RUN git -C cppcheck checkout 5ef5a772866c2760bb303086ee5befc3f9059564
WORKDIR /build/cppcheck
RUN mkdir build && cd build && cmake .. && cmake --build .

###################################
###### FINAL IMAGE
FROM alpine:3.24@sha256:be2656061b536c1883c4034790e21d0a09c5aaaf51968b89368a3c0371ea2c9a AS runtime
RUN apk update && \
    apk add --no-cache build-base=0.5-r4 cmake=4.2.3-r0 libstdc++=15.2.0-r5 python3=3.14.10-r1 py3-pip=26.1.2-r0

# cppcheck
COPY --from=build /build/cppcheck/build/bin/cppcheck /usr/bin
COPY --from=build /build/cppcheck/addons /usr/local/share/Cppcheck/addons
COPY --from=build /build/cppcheck/cfg/std.cfg /usr/local/share/Cppcheck/cfg/
COPY --from=build /build/cppcheck/htmlreport/cppcheck-htmlreport /usr/bin
COPY misra2023_rules_ascii.txt /usr/local/share/Cppcheck/addons/
RUN apk add py3-pygments
COPY requirements.txt /tmp/requirements.txt
RUN pip install --break-system-packages --no-cache-dir --require-hashes -r /tmp/requirements.txt && \
    rm /tmp/requirements.txt
