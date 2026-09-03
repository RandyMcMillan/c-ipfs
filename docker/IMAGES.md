# Official IPFS Docker Images

Reference: [ipfs on Docker Hub](https://hub.docker.com/u/ipfs)

## Active Repositories

| Image | Status | Description | Pulls |
|-------|--------|-------------|-------|
| [`ipfs/kubo`](https://hub.docker.com/r/ipfs/kubo) | active | Go implementation of IPFS (Kubo). Primary reference node for interoperability testing. | 20M+ |
| [`ipfs/ipfs-cluster`](https://hub.docker.com/r/ipfs/ipfs-cluster) | active | Pinset orchestration for IPFS. Use for cluster-based pinning tests. | 10M+ |

## Archived / Legacy Repositories

| Image | Status | Description |
|-------|--------|-------------|
| [`ipfs/go-ipfs`](https://hub.docker.com/r/ipfs/go-ipfs) | archived | Legacy images of Kubo published for backward-compatibility. |
| [`ipfs/js-ipfs`](https://hub.docker.com/r/ipfs/js-ipfs) | archived | IPFS implementation in JavaScript. |
| [`ipfs/bifrost-gateway`](https://hub.docker.com/r/ipfs/bifrost-gateway) | archived | Experimental IPFS Gateway implementation for ipfs.io and dweb.link. |
| `ipfs/ipfs-cluster-test` | archived | Testing container for IPFS Cluster. |
| `ipfs/ipfs-dns-deploy` | archived | DNS deployment helper. |
| `ipfs/ci-websites` | archived | CI Dashboard. |
| `ipfs/ci-sync` | archived | CI sync helper. |
| `ipfs/testground` | archived | Testground testing framework image. |
| `ipfs/ipfs-snap-builder` | archived | Snap package builder. |

## Quick Start: Kubo Test Node

```bash
docker run -d --name kubo-test \
  -p 4001:4001 -p 5001:5001 -p 8080:8080 \
  ipfs/kubo:latest
```

## Docker Compose for Interoperability Testing

See `docker/kubo-compose.yml` for a pre-configured Kubo node that can be used
to validate c-ipfs behavior against the reference Go implementation.
