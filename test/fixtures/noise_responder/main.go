package main

import (
	"context"
	"fmt"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/libp2p/go-libp2p"
	"github.com/libp2p/go-libp2p/core/crypto"
	"github.com/libp2p/go-libp2p/core/network"
	"github.com/libp2p/go-libp2p/p2p/security/noise"
	"github.com/multiformats/go-multiaddr"
)

type notifier struct{}

func (n *notifier) Listen(net network.Network, ma multiaddr.Multiaddr) {
	fmt.Printf("[net] Listen %v\n", ma)
}
func (n *notifier) ListenClose(net network.Network, ma multiaddr.Multiaddr) {
	fmt.Printf("[net] ListenClose %v\n", ma)
}
func (n *notifier) Connected(net network.Network, c network.Conn) {
	fmt.Printf("[net] Connected remote=%s direction=%v\n", c.RemotePeer(), c.Stat().Direction)
}
func (n *notifier) Disconnected(net network.Network, c network.Conn) {
	fmt.Printf("[net] Disconnected remote=%s\n", c.RemotePeer())
}
func (n *notifier) OpenedStream(net network.Network, s network.Stream) {
	fmt.Printf("[net] OpenedStream protocol=%s\n", s.Protocol())
}
func (n *notifier) ClosedStream(net network.Network, s network.Stream) {
	fmt.Printf("[net] ClosedStream\n")
}

func main() {
	port := "4003"
	if len(os.Args) > 1 {
		port = os.Args[1]
	}

	priv, _, err := crypto.GenerateKeyPair(crypto.Ed25519, -1)
	if err != nil {
		fmt.Fprintf(os.Stderr, "GenerateKeyPair: %v\n", err)
		os.Exit(1)
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	h, err := libp2p.New(
		libp2p.ListenAddrStrings(fmt.Sprintf("/ip4/127.0.0.1/tcp/%s", port)),
		libp2p.Identity(priv),
		libp2p.Security(noise.ID, noise.New),
	)
	if err != nil {
		fmt.Fprintf(os.Stderr, "libp2p.New: %v\n", err)
		os.Exit(1)
	}

	fmt.Printf("Listening addrs: %v\n", h.Addrs())
	fmt.Printf("Peer ID: %s\n", h.ID().String())

	h.Network().Notify(&notifier{})

	sig := make(chan os.Signal, 1)
	signal.Notify(sig, syscall.SIGINT, syscall.SIGTERM)
	<-sig

	_ = h.Close()
	<-ctx.Done()
	time.Sleep(100 * time.Millisecond)
}
