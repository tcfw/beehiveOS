package network

import (
	"net"
	"sync"
	"sync/atomic"
	"testing"

	"github.com/tcfw/kernel/services/go/utils"
)

type MockInterfaceHandler struct {
	Received    []*Packet
	wait        chan struct{}
	mu          sync.Mutex
	DropOnEnque bool
}

func (m *MockInterfaceHandler) WaitForPacket() {
	if len(m.Received) > 0 {
		return
	}
	<-m.wait
}

func NewMockInterfaceHandler() *MockInterfaceHandler {
	return &MockInterfaceHandler{
		Received: make([]*Packet, 0),
		wait:     make(chan struct{}),
	}
}

func (m *MockInterfaceHandler) Done() {
	for _, p := range m.Received {
		p.Done()
	}
}

func (m *MockInterfaceHandler) QueueDisc() QueueDiscipline {
	return QueueDisciplineFIFO
}

func (m *MockInterfaceHandler) Len() uint {
	return 1
}

func (m *MockInterfaceHandler) Poll() error {
	return nil
}

func (m *MockInterfaceHandler) Enqueue(p *Packet) (error, bool) {
	if m.DropOnEnque {
		p.Done()
		return nil, true
	}

	m.mu.Lock()
	defer m.mu.Unlock()

	m.Received = append(m.Received, p)

	select {
	case m.wait <- struct{}{}:
	default:
	}

	return nil, true
}

func (m *MockInterfaceHandler) Dequeue() (*Packet, error, bool) {
	m.mu.Lock()
	defer m.mu.Unlock()

	if len(m.Received) == 0 {
		return nil, nil, false
	}

	p := m.Received[0]
	m.Received = m.Received[1:]
	return p, nil, true
}

func TestHandleEthernetToICMPv6(t *testing.T) {
	pingFrame := []byte{
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, //ethernet dest
		0x76, 0xac, 0xb9, 0xd9, 0xc7, 0xbe, //ethernet src
		0x86, 0xdd, //etherType IPv6
		0x60, 0x0d, 0x0b, 0x00, // IPv6, TC, FL
		0x00, 0x08, 0x3a, 0xff, // Length (8), Next Header (ICMPv6), Hop Limit
		0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x0b, 0x6e, 0x1d, 0x37, 0xcd, 0x7a, 0x70, // Src
		0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, // Dest
		0x80, 0x00, 0x49, 0xD0, //ICMPv6 (RS), Code, CSUM
		0x00, 0x00, 0x00, 0x01, //Body
	}

	naFrame := []byte{
		0x76, 0xac, 0xb9, 0xd9, 0xc7, 0xbe, //ethernet src
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, //ethernet dest
		0x86, 0xdd, //etherType IPv6
		0x60, 0x0d, 0x0b, 0x00, // IPv6, TC, FL
		0x00, 0x20, 0x3a, 0xff, // Length (8), Next Header (ICMPv6), Hop Limit
		0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xff, 0xcd, 0x7a, 0x70, // Src
		0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, // Dst
		0x88, 0x00, 0x8e, 0xb2, //ICMPv6 (NA), Code, CSUM
		0x40, 0x00, 0x00, 0x00, //Flags
		0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x0b, 0x6e, 0x1d, 0x37, 0xcd, 0x7a, 0x70, //Target
		0x02, 0x01, 0x76, 0xac, 0xb9, 0xd9, 0xc7, 0xbe, //MAC Address
	}

	mi := NewMockInterfaceHandler()

	lo := LoopbackInterface{
		Interface: Interface{
			HardwareAddr:    MacAddress{1, 2, 3, 4, 5, 6},
			NetSpace:        DefaultNetSpace(),
			MTU:             1500,
			DefaultHopLimit: 255,
			Handlers:        mi,
		},
	}

	ip, ipm, err := net.ParseCIDR("ff02::2/128")
	if err != nil {
		t.Fatal(err)
	}
	ipm.IP = ip

	ip2, ipm2, err := net.ParseCIDR("fe80::2/64")
	if err != nil {
		t.Fatal(err)
	}
	ipm2.IP = ip2

	lo.IPAddrs = append(lo.IPAddrs, IP{*ipm2, IPFlagStatic})
	lo.SubscribedIPAddrs = append(lo.SubscribedIPAddrs, IP{*ipm, IPFlagStatic})

	lo.NetSpace.FIB.Insert(ForwardingRule{
		Family:    AddressFamilyIPv6,
		Prefix:    *ipm2,
		Interface: &lo.Interface,
		Scope:     FRSOnInterface,
	})

	pingPacket := GetPacket()
	pingPacket.Frame = append(pingPacket.Frame, pingFrame...)
	pingPacket.Payload = pingPacket.Frame[0:]
	pingPacket.SrcDevice = &lo.Interface

	naPacket := GetPacket()
	naPacket.Frame = append(naPacket.Frame, naFrame...)
	naPacket.Payload = naPacket.Frame[0:]
	naPacket.SrcDevice = &lo.Interface

	didReachICMPv6 := false

	chook := HookICMPv6PacketRX
	t.Cleanup(func() {
		HookICMPv6PacketRX = chook
	})

	HookICMPv6PacketRX = func(p *Packet) HookFnAction {
		didReachICMPv6 = true
		return HookFnAction{HookActionNOOP, nil}
	}

	HandleEthernetFrame(pingPacket)

	if didReachICMPv6 == false {
		t.Fatal("failed to reach ICMPv6 handler")
	}

	mi.WaitForPacket()
	rp, _, _ := mi.Dequeue()
	t.Logf("%X", rp.Frame)
	rp.Done()

	HandleEthernetFrame(naPacket)

	mi.WaitForPacket()
	rp, _, _ = mi.Dequeue()
	t.Logf("%X", rp.Frame)
	rp.Done()

	pingPacket = GetPacket()
	pingPacket.Frame = append(pingPacket.Frame, pingFrame...)
	pingPacket.Payload = pingPacket.Frame[0:]
	pingPacket.SrcDevice = &lo.Interface
	HandleEthernetFrame(pingPacket)

	mi.WaitForPacket()
	rp, _, _ = mi.Dequeue()
	t.Logf("%X", rp.Frame)
	rp.Done()

	returned := atomic.LoadUint64(&returnedPacketCount)
	used := atomic.LoadUint64(&getPacketCount)
	if returned != used {
		t.Fatalf("packet leak/inbalance, used %d, returned %d", used, returned)
	}
}

func BenchmarkHandlePing(b *testing.B) {
	pingFrame := []byte{
		0x01, 0x02, 0x03, 0x04, 0x05, 0x06, //ethernet dest
		0x76, 0xac, 0xb9, 0xd9, 0xc7, 0xbe, //ethernet src
		0x86, 0xdd, //etherType IPv6
		0x60, 0x0d, 0x0b, 0x00, // IPv6, TC, FL
		0x00, 0x08, 0x3a, 0xff, // Length (8), Next Header (ICMPv6), Hop Limit
		0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x0b, 0x6e, 0x1d, 0x37, 0xcd, 0x7a, 0x70, // Src
		0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, // Dest
		0x80, 0x00, 0x49, 0xD0, //ICMPv6 (RS), Code, CSUM
		0x00, 0x00, 0x00, 0x01, //Body
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	}

	mi := NewMockInterfaceHandler()
	mi.DropOnEnque = true

	lo := LoopbackInterface{
		Interface: Interface{
			HardwareAddr:    MacAddress{1, 2, 3, 4, 5, 6},
			NetSpace:        DefaultNetSpace(),
			MTU:             1500,
			DefaultHopLimit: 255,
			Handlers:        mi,
			Capabilities:    InterfaceCapabilitiesHWCSUM | InterfaceCapabilitiesIPV6CSUM, //skip ICMPv6 checksums
		},
	}

	ip, ipm, err := net.ParseCIDR("ff02::2/128")
	if err != nil {
		b.Fatal(err)
	}
	ipm.IP = ip

	ip2, ipm2, err := net.ParseCIDR("fe80::2/64")
	if err != nil {
		b.Fatal(err)
	}
	ipm2.IP = ip2

	lo.IPAddrs = append(lo.IPAddrs, IP{*ipm2, IPFlagStatic})
	lo.SubscribedIPAddrs = append(lo.SubscribedIPAddrs, IP{*ipm, IPFlagStatic})

	lo.NetSpace.FIB.Insert(ForwardingRule{
		Family:    AddressFamilyIPv6,
		Prefix:    *ipm2,
		Interface: &lo.Interface,
		Scope:     FRSOnInterface,
	})
	lo.NetSpace.Neighbours.Set(net.IP{0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x0b, 0x6e, 0x1d, 0x37, 0xcd, 0x7a, 0x70}, MacAddress{0x76, 0xac, 0xb9, 0xd9, 0xc7, 0xbe}, &lo.Interface, 0)

	b.ResetTimer()

	//keep the same ping packet, just reset it
	// pingPacket.Done = func() {
	// 	pingPacket.reset()
	// 	pingPacket.Frame = pingPacket.Frame[:len(pingFrame)]
	// 	pingPacket.Payload = pingPacket.Frame[0:]
	// }

	b.SetBytes(int64(len(pingFrame)))

	ring := utils.NewRing(1500, 1000)

	done := make(chan struct{})

	go func() {
		for {
			select {
			case <-done:
				return
			default:
			}

			ring.Push(pingFrame)
		}
	}()

	for n := 0; n < b.N; n++ {
		// pingPacket := GetPacket()
		// pingPacket.Frame = append(pingPacket.Frame, pingFrame...)
		// pingPacket.Frame = pingPacket.Frame[:len(pingFrame)]
		// copy(pingPacket.Frame, pingFrame)
		// pingPacket.Payload = pingPacket.Frame[0:]
		pingPacket := PacketFromRing(ring)
		pingPacket.SrcDevice = &lo.Interface

		// handlePacket(pingPacket)

		// trace.WithRegion(context.Background(), "handlePacket", func() {
		HandleEthernetFrame(pingPacket)
		// })
	}

	close(done)

}