// Package mailer delivers the transactional email the website API sends, namely
// registration confirmation codes. It offers two transports: an SMTP transport
// for real delivery and a log transport for development, selected by config.
package mailer

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"net"
	"net/smtp"
	"strings"
	"time"
)

// Message is a single plain-text email.
type Message struct {
	To      string
	Subject string
	Body    string
}

// Mailer delivers a Message. Implementations must be safe for concurrent use.
type Mailer interface {
	Send(ctx context.Context, message Message) error
}

func (m Message) validate() error {
	if strings.TrimSpace(m.To) == "" {
		return errors.New("mailer: recipient is required")
	}
	if strings.ContainsAny(m.To, "\r\n") || strings.ContainsAny(m.Subject, "\r\n") {
		return errors.New("mailer: header injection detected")
	}
	return nil
}

// LogMailer records outgoing mail to a logger instead of sending it. It is the
// development default so a local operator can read confirmation codes from the
// service log without a mail server.
type LogMailer struct {
	Logger *slog.Logger
	From   string
}

// NewLogMailer returns a LogMailer. A nil logger disables output.
func NewLogMailer(logger *slog.Logger, from string) *LogMailer {
	return &LogMailer{Logger: logger, From: from}
}

// Send logs the message. The body is included deliberately: in development the
// confirmation code is only useful if the operator can read it.
func (l *LogMailer) Send(_ context.Context, message Message) error {
	if err := message.validate(); err != nil {
		return err
	}
	if l.Logger == nil {
		return nil
	}
	l.Logger.Info("email (log transport, not delivered)",
		"from", l.From, "to", message.To, "subject", message.Subject, "body", message.Body)
	return nil
}

// SMTPMailer delivers mail through an SMTP relay.
type SMTPMailer struct {
	Addr     string // host:port
	From     string
	auth     smtp.Auth
	host     string
	dialer   *net.Dialer
	sendMail func(addr string, a smtp.Auth, from string, to []string, msg []byte) error
}

// SMTPConfig configures an SMTPMailer.
type SMTPConfig struct {
	Host     string
	Port     int
	Username string
	Password string
	From     string
}

// NewSMTPMailer validates config and returns an SMTPMailer. When a username is
// supplied, PLAIN authentication is used (only over TLS-capable relays).
func NewSMTPMailer(config SMTPConfig) (*SMTPMailer, error) {
	if strings.TrimSpace(config.Host) == "" {
		return nil, errors.New("mailer: smtp host is required")
	}
	if config.Port < 1 || config.Port > 65535 {
		return nil, fmt.Errorf("mailer: invalid smtp port %d", config.Port)
	}
	if strings.TrimSpace(config.From) == "" {
		return nil, errors.New("mailer: smtp from address is required")
	}
	mailer := &SMTPMailer{
		Addr:     net.JoinHostPort(config.Host, fmt.Sprintf("%d", config.Port)),
		From:     config.From,
		host:     config.Host,
		sendMail: smtp.SendMail,
	}
	if config.Username != "" {
		mailer.auth = smtp.PlainAuth("", config.Username, config.Password, config.Host)
	}
	return mailer, nil
}

// Send delivers the message via SMTP.
func (s *SMTPMailer) Send(_ context.Context, message Message) error {
	if err := message.validate(); err != nil {
		return err
	}
	raw := buildMessage(s.From, message)
	if err := s.sendMail(s.Addr, s.auth, s.From, []string{message.To}, raw); err != nil {
		return fmt.Errorf("mailer: send: %w", err)
	}
	return nil
}

func buildMessage(from string, message Message) []byte {
	var builder strings.Builder
	builder.WriteString("From: " + from + "\r\n")
	builder.WriteString("To: " + message.To + "\r\n")
	builder.WriteString("Subject: " + message.Subject + "\r\n")
	builder.WriteString("Date: " + time.Now().UTC().Format(time.RFC1123Z) + "\r\n")
	builder.WriteString("MIME-Version: 1.0\r\n")
	builder.WriteString("Content-Type: text/plain; charset=\"utf-8\"\r\n")
	builder.WriteString("\r\n")
	builder.WriteString(strings.ReplaceAll(message.Body, "\n", "\r\n"))
	return []byte(builder.String())
}
