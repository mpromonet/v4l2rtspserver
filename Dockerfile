FROM heroku/heroku:16
LABEL maintainer michel.promonet@free.fr

WORKDIR /v4l2rtspserver
ADD . /v4l2rtspserver

RUN apt-get update && apt-get install -y --no-install-recommends g++ autoconf automake libtool xz-utils cmake liblog4cpp5-dev libx264-dev libjpeg-dev v4l2loopback-dkms pkg-config git wget
RUN git clone --depth 1 https://github.com/mpromonet/v4l2tools.git

RUN cmake . && make \
	&& make -C v4l2tools \
	&& apt-get clean

EXPOSE 8554

ENTRYPOINT [ "./v4l2rtspserver" ]
CMD [ "-S -P ${PORT}" ]
