###### BUILD IMAGE
FROM alpine:3.17@sha256:8fc3dacfb6d69da8d44e42390de777e48577085db99aa4e4af35f483eb08b989 AS build

# Several installs
RUN apk update
RUN apk add git cmake ninja build-base python3 make

# cppcheck
WORKDIR /build
RUN git clone https://github.com/RomainPelletant/cppcheck --branch addon/misra/export-result-to-xml-junit
WORKDIR /build/cppcheck
RUN mkdir build && cd build && cmake .. && cmake --build .

###################################
###### FINAL IMAGE
FROM alpine:3.17@sha256:8fc3dacfb6d69da8d44e42390de777e48577085db99aa4e4af35f483eb08b989 AS runtime
RUN apk update && \
    apk add --no-cache build-base cmake libstdc++ python3 py3-pip
# Python is installed

# cppcheck
COPY --from=build /build/cppcheck/build/bin/cppcheck /usr/bin
COPY --from=build /build/cppcheck/addons /usr/local/share/Cppcheck/addons
COPY --from=build /build/cppcheck/cfg/std.cfg /usr/local/share/Cppcheck/cfg/
COPY --from=build /build/cppcheck/htmlreport/cppcheck-htmlreport /usr/bin
COPY misra2023_rules_ascii.txt /usr/local/share/Cppcheck/addons/
RUN apk add py3-pygments
RUN pip install --break-system-packages junit-xml
RUN pip install --break-system-packages cppcheck-junit
RUN pip install --break-system-packages flawfinder

###################################
###### DEVELOPMENT IMAGE
FROM runtime AS development
RUN apk add --no-cache bash git ninja
RUN addgroup -g 1000 vscode && \
    adduser -D -u 1000 -G vscode vscode
USER vscode
