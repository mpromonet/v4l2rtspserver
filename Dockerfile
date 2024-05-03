ARG IMAGE=ubuntu:24.04
FROM $IMAGE as builder
LABEL maintainer michel.promonet@free.fr
WORKDIR /v4l2rtspserver

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ca-certificates g++ autoconf automake libtool xz-utils cmake make patch pkg-config git wget libasound2-dev libssl-dev 
COPY . .

RUN cmake . && make install && apt-get clean && rm -rf /var/lib/apt/lists/

FROM $IMAGE
WORKDIR /usr/local/share/v4l2rtspserver

RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends ca-certificates libasound2-dev libssl-dev && apt-get clean && rm -rf /var/lib/apt/lists/

COPY --from=builder /usr/local/bin/ /usr/local/bin/
COPY --from=builder /usr/local/share/v4l2rtspserver/ /usr/local/share/v4l2rtspserver/

ENTRYPOINT [ "/usr/local/bin/v4l2rtspserver" ]
CMD [ "-S" ]
