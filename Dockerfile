FROM alpine:3.20 AS build

RUN apk add --no-cache build-base zlib-dev curl
WORKDIR /src
COPY . .
ARG RINHA_INDEX_V2=0
ARG RINHA_INDEX_KD_TREE=1
ARG RINHA_KD_LEAF_SIZE=192
ARG RINHA_IVF_LISTS=4096
ARG RINHA_KMEANS_TRAIN=131072
ARG RINHA_KMEANS_ITERS=3
ARG RINHA_KMEANS_WINDOW=64
RUN make clean all CFLAGS_ARCH="-march=haswell -mtune=haswell -mavx2 -mfma -flto -fomit-frame-pointer -fno-plt -fno-semantic-interposition -fno-trapping-math -DRINHA_ASSUME_PASSED_FD_FLAGS"
RUN mkdir -p /src/resources \
    && curl -fsSL -o /tmp/references.json.gz https://raw.githubusercontent.com/zanfranceschi/rinha-de-backend-2026/main/resources/references.json.gz \
    && RINHA_INDEX_V2="$RINHA_INDEX_V2" RINHA_INDEX_KD_TREE="$RINHA_INDEX_KD_TREE" RINHA_KD_LEAF_SIZE="$RINHA_KD_LEAF_SIZE" RINHA_KMEANS_TRAIN="$RINHA_KMEANS_TRAIN" RINHA_KMEANS_ITERS="$RINHA_KMEANS_ITERS" RINHA_KMEANS_WINDOW="$RINHA_KMEANS_WINDOW" ./build/build-index /tmp/references.json.gz /src/resources/index.bin "$RINHA_IVF_LISTS" \
    && strip build/api

FROM alpine:3.20

RUN adduser -D -H rinha
WORKDIR /app
COPY --from=build /src/build/api /app/api
COPY --from=build /src/resources/index.bin /app/resources/index.bin
USER rinha
CMD ["/app/api"]
