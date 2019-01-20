FROM debian
LABEL maintainer michel.promonet@free.fr

WORKDIR /v4l2rtspserver
COPY . /v4l2rtspserver

RUN apt-get update && apt-get install -y --no-install-recommends g++ autoconf automake libtool xz-utils cmake liblog4cpp5-dev pkg-config git wget

RUN cmake . && make \
	&& apt-get clean

EXPOSE 8554

ENTRYPOINT [ "./v4l2rtspserver" ]
CMD [ "-S -P ${PORT}" ]
