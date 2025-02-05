package drivers

import (
	"github.com/tcfw/kernel/services/go/fs/drivers/block"
	"github.com/tcfw/kernel/services/go/utils"
)

var (
	drivers = map[string]BlockInitFn{}
)

type BlockInitFn func(utils.DevInfo) error

type BlockQueuer interface {
	BlockSize() uint64
	IsBusy() bool
	QueueSize() uint64
	Enqueue(reqs []block.IORequest, comp chan<- block.IOResponse) (error, int)
}

type BlockDriver interface {
	BlockQueuer
	Watch() error
	StopWatch()
}

func RegisterDriver(compat string, driver BlockInitFn) {
	drivers[compat] = driver
}

func FindDeviceDriver(compat string) (BlockInitFn, bool) {
	driver, ok := drivers[compat]
	return driver, ok
}
