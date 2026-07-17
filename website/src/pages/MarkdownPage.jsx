import { createMemo } from "solid-js";
import { marked } from "marked";

export function MarkdownPage(props) {
  const html = createMemo(() => marked.parse(props.markdown));

  return (
    <article class="document-page">
      <div innerHTML={html()} />
    </article>
  );
}
