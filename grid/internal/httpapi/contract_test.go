package httpapi

import (
	"bufio"
	"encoding/json"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
)

type contractCase struct {
	method string
	path   string
	status int
	schema string
}

func TestOperationalContract(t *testing.T) {
	handler := New(checker{}, "contract-test", Options{})
	for _, contract := range loadOperationalContract(t) {
		name := contract.method + " " + contract.path
		t.Run(name, func(t *testing.T) {
			r := httptest.NewRequest(contract.method, contract.path, nil)
			w := httptest.NewRecorder()
			handler.ServeHTTP(w, r)

			if w.Code != contract.status {
				t.Fatalf("status = %d, want %d", w.Code, contract.status)
			}
			if !validRequestID(w.Header().Get(RequestIDHeader)) {
				t.Fatalf("invalid response request ID %q", w.Header().Get(RequestIDHeader))
			}
			assertSchema(t, w.Body.Bytes(), contract.schema)
		})
	}
}

func loadOperationalContract(t *testing.T) []contractCase {
	t.Helper()
	path := filepath.Join("..", "..", "..", "api", "contracts", "operational.tsv")
	file, err := os.Open(path)
	if err != nil {
		t.Fatalf("open operational contract: %v", err)
	}
	defer file.Close()

	var contracts []contractCase
	scanner := bufio.NewScanner(file)
	for lineNumber := 1; scanner.Scan(); lineNumber++ {
		line := scanner.Text()
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		fields := strings.Split(line, "\t")
		if len(fields) != 4 {
			t.Fatalf("%s:%d: expected four tab-separated fields", path, lineNumber)
		}
		status, err := strconv.Atoi(fields[2])
		if err != nil {
			t.Fatalf("%s:%d: invalid status: %v", path, lineNumber, err)
		}
		contracts = append(contracts, contractCase{
			method: fields[0], path: fields[1], status: status, schema: fields[3],
		})
	}
	if err := scanner.Err(); err != nil {
		t.Fatalf("read operational contract: %v", err)
	}
	if len(contracts) == 0 {
		t.Fatal("operational contract contains no cases")
	}
	return contracts
}

func assertSchema(t *testing.T, body []byte, schema string) {
	t.Helper()
	var value map[string]any
	if err := json.Unmarshal(body, &value); err != nil {
		t.Fatalf("decode %s response: %v\n%s", schema, err, body)
	}
	required := map[string][]string{
		"Status":  {"status"},
		"Version": {"service", "version", "apiVersion"},
		"Error":   {"code", "message"},
	}[schema]
	if required == nil {
		t.Fatalf("unknown contract schema %q", schema)
	}
	if len(value) != len(required) {
		t.Fatalf("%s response fields = %v, want exactly %v", schema, value, required)
	}
	for _, field := range required {
		if text, ok := value[field].(string); !ok || text == "" {
			t.Errorf("%s field %q = %#v, want a non-empty string", schema, field, value[field])
		}
	}
}
