package main

import (
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"net"
	"os"
	"strconv"
	"time"
)

var listenInterface string

func init() {
	flag.StringVar(&listenInterface, "interface", "", "interface on which to listen for messages")
}

type UnixTime struct {
	time.Time
}

func (t *UnixTime) UnmarshalJSON(data []byte) error {
	epoch, err := strconv.ParseUint(string(data), 10, 32)
	if err != nil {
		return err
	}
	*t = UnixTime{Time: time.Unix(int64(epoch), 0)}
	return nil
}

type sensor_event struct {
	Id    string   `json:"id"`
	Epoch UnixTime `json:"epoch"`
	Type  string   `json:"type"`
	Data  float32  `json:"data"`
}

func handleError(err error) {
	if err == nil {
		return
	}
	fmt.Fprintln(os.Stderr, err)
	os.Exit(1)
}

func convertCtoF(c float32) float32 {
	return c*1.8 + 32
}

func main() {
	flag.Parse()
	iface, err := net.InterfaceByName(listenInterface)
	handleError(err)
	addr, err := net.ResolveUDPAddr("udp", "239.0.10.1:10000")
	handleError(err)
	conn, err := net.ListenMulticastUDP("udp", iface, addr)
	handleError(err)
	for {
		var evt sensor_event
		buf := make([]byte, 4096)
		n, err := conn.Read(buf)
		if err != nil {
			fmt.Fprintln(os.Stderr, err)
		}
		if n == 0 {
			continue
		}
		decoder := json.NewDecoder(bytes.NewReader(buf))
		err = decoder.Decode(&evt)
		if err != nil {
			fmt.Fprintln(os.Stderr, err)
		}
		switch evt.Type {
		case "temperature":
			fmt.Printf("%s: (%s) Temperature: %.2f *F\n",
				evt.Epoch,
				evt.Id,
				convertCtoF(evt.Data))
		case "humidity":
			fmt.Printf("%s: (%s) Humidity: %.2f%%\n",
				evt.Epoch,
				evt.Id,
				evt.Data)
		}
	}
}
