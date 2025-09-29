ARG IMAGE=debian:trixie
FROM $IMAGE AS builder
LABEL maintainer michel.promonet@free.fr
WORKDIR /v4l2rtspserver

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ca-certificates g++ autoconf automake libtool xz-utils cmake make patch pkg-config git wget libasound2-dev libssl-dev ninja 
COPY . .

RUN cmake -S . -B build -G Ninja && cmake --build build && cmake --install build && apt-get clean && rm -rf /var/lib/apt/lists/

FROM $IMAGE
WORKDIR /usr/local/share/v4l2rtspserver

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ca-certificates libasound2-dev libssl-dev && apt-get clean && rm -rf /var/lib/apt/lists/

COPY --from=builder /usr/local/bin/ /usr/local/bin/
COPY --from=builder /usr/local/share/v4l2rtspserver/ /usr/local/share/v4l2rtspserver/

ENTRYPOINT [ "/usr/local/bin/v4l2rtspserver" ]
CMD [ "-S" ]
