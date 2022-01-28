FROM ubuntu:20.04 as builder
LABEL maintainer michel.promonet@free.fr
WORKDIR /v4l2rtspserver
COPY . /v4l2rtspserver

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ca-certificates g++ autoconf automake libtool xz-utils cmake make pkg-config git wget libasound2-dev libssl-dev \
    && cmake . && make install && apt-get clean && rm -rf /var/lib/apt/lists/

FROM ubuntu:20.04
WORKDIR /usr/local/share/v4l2rtspserver
COPY --from=builder /usr/local/bin/ /usr/local/bin/
COPY --from=builder /usr/local/share/v4l2rtspserver/ /usr/local/share/v4l2rtspserver/

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ca-certificates libasound2 libssl1.1 && apt-get clean && rm -rf /var/lib/apt/lists/

ENTRYPOINT [ "/usr/local/bin/v4l2rtspserver" ]
CMD [ "-S" ]
