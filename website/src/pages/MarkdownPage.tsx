import { createMemo } from "solid-js";
import { marked } from "marked";

interface MarkdownPageProps {
  markdown: string;
}

export function MarkdownPage(props: MarkdownPageProps) {
  const html = createMemo(() => marked.parse(props.markdown) as string);

  return (
    <article class="document-page">
      <div innerHTML={html()} />
    </article>
  );
}
