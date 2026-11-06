package main

import (
	"bytes"
	"context"
	_ "embed"
	"fmt"
	"github.com/alecthomas/kong"
	"google.golang.org/genai"
	"log"
	"os"
	"strings"
	"text/template"
)

//go:embed prompt.tmpl
var promptTmpl string

var CLI struct {
	IRFile  string `arg:"" name:"ir-file" help:"Path to LLVM IR file" type:"path"`
	AsmFile string `arg:"" name:"asm-file" help:"Path to RISC-V Assembly file" type:"path"`

	/* Optional */
	Out   string `help:"Output file path" default:"annotated.S"`
	Model string `help:"Model to use" default:"gemini-3-flash-preview"`
}

type PromptData struct {
	IR  string
	ASM string
}

func main() {
	_ = kong.Parse(&CLI,
		kong.Name("asm_annotate"),
		kong.Description("Annotate RISC-V Assembly using LLVM IR"),
		kong.UsageOnError(),
	)

	irContent, err := os.ReadFile(CLI.IRFile)
	if err != nil {
		log.Fatalf("Failed to read IR: %v", err)
	}
	asmContent, err := os.ReadFile(CLI.AsmFile)
	if err != nil {
		log.Fatalf("Failed to read ASM: %v", err)
	}

	ctx := context.Background()
	client, err := genai.NewClient(ctx, nil)
	if err != nil {
		log.Fatalf("Failed to create client: %v", err)
	}

	t, err := template.New("prompt").Parse(string(promptTmpl))
	if err != nil {
		log.Fatalf("Failed to parse template: %v", err)
	}

	var promptBuf bytes.Buffer
	err = t.Execute(&promptBuf, PromptData{
		IR:  string(irContent),
		ASM: string(asmContent),
	})
	if err != nil {
		log.Fatalf("Template execution error: %v", err)
	}

	config := &genai.GenerateContentConfig{
		ThinkingConfig: &genai.ThinkingConfig{
			IncludeThoughts: false,
			ThinkingLevel:   genai.ThinkingLevelHigh,
		},
	}

	fmt.Printf("Generating annotations using %s...\n", CLI.Model)
	resp, err := client.Models.GenerateContent(
		ctx, CLI.Model, genai.Text(promptBuf.String()), config)
	if err != nil {
		log.Fatalf("API Error: %v", err)
	}

	if len(resp.Candidates) == 0 || len(resp.Candidates[0].Content.Parts) == 0 {
		log.Fatal("No response from model")
	}

	if resp.UsageMetadata != nil {
		fmt.Printf("--- Token Usage ---\n")
		fmt.Printf("Input Tokens:      %d\n", resp.UsageMetadata.PromptTokenCount)
		fmt.Printf("Output Tokens:     %d (Includes Thinking)\n", resp.UsageMetadata.CandidatesTokenCount)
		fmt.Printf("Total Tokens:      %d\n", resp.UsageMetadata.TotalTokenCount)
	}

	var resultText string
	for _, part := range resp.Candidates[0].Content.Parts {
		if part.Text != "" {
			resultText += part.Text
		}
	}

	/* remove markdown block */
	lines := strings.Split(resultText, "\n")
	var finalLines []string
	for _, line := range lines {
		if strings.HasPrefix(strings.TrimSpace(line), "```") {
			continue
		}
		finalLines = append(finalLines, line)
	}

	err = os.WriteFile(CLI.Out, []byte(strings.Join(finalLines, "\n")), 0644)
	if err != nil {
		log.Fatalf("Failed to write output: %v", err)
	}
	fmt.Printf("Annotated assembly written to: %s\n", CLI.Out)
}
