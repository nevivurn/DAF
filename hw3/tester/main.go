package main

import (
	"bytes"
	"context"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"time"
)

type work struct {
	query, dag string
}
type result struct {
	pass           bool
	elapsed, calls float64
}

func main() {
	queryData, err := ioutil.ReadFile(os.Args[2])
	if err != nil {
		panic(err)
	}
	var queries []string
	for _, query := range bytes.Split(queryData, []byte("t")) {
		if len(query) > 0 {
			queries = append(queries, "t"+string(query))
		}
	}

	dagData, err := ioutil.ReadFile(os.Args[3])
	if err != nil {
		panic(err)
	}
	var dags []string
	for _, dag := range bytes.Split(dagData, []byte("\n")) {
		if len(dag) > 0 {
			dags = append(dags, string(dag)+"\n")
		}
	}

	wc := make(chan work)
	go func() {
		for i, d := range dags {
			wc <- work{queries[i], d}
		}
		close(wc)
	}()

	rcs := make([]<-chan result, runtime.NumCPU()/2)
	for i := 0; i < runtime.NumCPU()/2; i++ {
		rcs[i] = worker(wc)
	}

	var (
		elapsed float64
		calls   float64
		passed  int
	)
	rc := merge(rcs...)
	for r := range rc {
		if !r.pass {
			fmt.Print("!")
			continue
		}
		elapsed += r.elapsed
		calls += r.calls
		passed++
		fmt.Print(".")
	}

	fmt.Println()
	fmt.Println("elapsed:", elapsed/float64(passed))
	fmt.Println("calls:", calls/float64(passed))
	fmt.Println("passed:", passed, "/", len(dags))
}

func merge(rcs ...<-chan result) <-chan result {
	out := make(chan result)

	var wg sync.WaitGroup
	wg.Add(len(rcs))

	for _, rc := range rcs {
		go func(rc <-chan result) {
			for r := range rc {
				out <- r
			}
			wg.Done()
		}(rc)
	}

	go func() {
		wg.Wait()
		close(out)
	}()

	return out
}

func worker(wc <-chan work) <-chan result {
	out := make(chan result)

	go func() {
		for w := range wc {
			out <- doWork(w)
		}
		close(out)
	}()

	return out
}

func doWork(w work) result {
	res := result{pass: false}

	qf, err := ioutil.TempFile("", "daf-test-q-")
	if err != nil {
		log.Println(err)
		return res
	}
	defer qf.Close()
	defer os.Remove(qf.Name())
	if _, err := fmt.Fprint(qf, w.query); err != nil {
		log.Println(err)
		return res
	}

	df, err := ioutil.TempFile("", "daf-test-dag-")
	if err != nil {
		log.Println(err)
		return res
	}
	defer df.Close()
	defer os.Remove(df.Name())
	if _, err := fmt.Fprint(df, w.dag); err != nil {
		log.Println(err)
		return res
	}

	ctx := context.Background()
	ctx, cancel := context.WithTimeout(ctx, 2*time.Minute-5*time.Second)
	defer cancel()

	cmd := exec.CommandContext(ctx, "./daf",
		"-d", os.Args[1], "-q", qf.Name(), "-a", df.Name(), "-n", "1")
	out, err := cmd.Output()
	if err != nil {
		return res
	}

	stats := bytes.Split(out, []byte("\n"))
	stats = stats[len(stats)-5 : len(stats)-3]

	res.pass = true
	res.elapsed, _ = strconv.ParseFloat(strings.Fields(string(stats[0]))[5], 64)
	res.calls, _ = strconv.ParseFloat(strings.Fields(string(stats[1]))[3], 64)

	return res
}
