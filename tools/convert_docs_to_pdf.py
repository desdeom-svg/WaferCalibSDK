import os
import re
import base64
import subprocess
import markdown

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    if os.path.basename(script_dir).lower() == 'tools':
        base_dir = os.path.dirname(script_dir)
    else:
        base_dir = script_dir

    docs_dir = os.path.join(base_dir, 'docs')
    md_path = os.path.join(docs_dir, 'WaferCalibSDK_C_API调用说明.md')
    html_path = os.path.join(docs_dir, 'WaferCalibSDK_C_API调用说明.html')
    pdf_path = os.path.join(docs_dir, 'WaferCalibSDK_C_API调用说明.pdf')

    print(f"Reading markdown from: {md_path}")
    with open(md_path, 'r', encoding='utf-8') as f:
        md_text = f.read()

    # 1. 将 markdown 中引用的 images/*.png 转为 Base64 内嵌图片，确保 PDF 渲染绝对不丢图
    def embed_base64_image(match):
        alt = match.group(1)
        rel_path = match.group(2)
        full_img_path = os.path.normpath(os.path.join(docs_dir, rel_path))
        if os.path.exists(full_img_path):
            with open(full_img_path, 'rb') as img_f:
                b64_data = base64.b64encode(img_f.read()).decode('ascii')
            mime = 'image/png' if rel_path.lower().endswith('.png') else 'image/jpeg'
            return f'![{alt}](data:{mime};base64,{b64_data})'
        else:
            print(f"Warning: image not found: {full_img_path}")
            return match.group(0)

    md_text = re.sub(r'!\[(.*?)\]\((images/.*?)\)', embed_base64_image, md_text)

    # 2. 将 ```mermaid 转换为 <div class="mermaid">
    def replace_mermaid(match):
        code = match.group(1).strip()
        return f'<div class="mermaid">\n{code}\n</div>'

    md_text = re.sub(r'```mermaid\s*\n(.*?)\n```', replace_mermaid, md_text, flags=re.DOTALL)

    # 3. 使用 Python markdown 将 markdown 转为 html
    html_body = markdown.markdown(
        md_text,
        extensions=['tables', 'fenced_code', 'toc']
    )

    # 4. 生成带有优雅印刷样式的 HTML
    html_content = f'''<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8">
<title>WaferCalibSDK 视觉算法库接口与开发指南</title>
<script>
MathJax = {{
  tex: {{
    inlineMath: [['$', '$'], ['\\\\(', '\\\\)']],
    displayMath: [['$$', '$$'], ['\\\\[', '\\\\]']]
  }},
  svg: {{ fontCache: 'global' }}
}};
</script>
<script id="MathJax-script" async src="https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-mml-chtml.js"></script>
<script src="https://cdn.jsdelivr.net/npm/mermaid@10/dist/mermaid.min.js"></script>
<script>
mermaid.initialize({{
  startOnLoad: true,
  theme: 'neutral',
  flowchart: {{ useMaxWidth: true, htmlLabels: true }}
}});
</script>
<style>
@page {{
  size: A4;
  margin: 15mm 15mm 15mm 15mm;
}}
body {{
  font-family: "Microsoft YaHei", "Segoe UI", -apple-system, BlinkMacSystemFont, "PingFang SC", sans-serif;
  font-size: 12.5px;
  line-height: 1.6;
  color: #1a202c;
  background-color: #fff;
  margin: 0;
  padding: 0;
}}
h1 {{
  font-size: 22px;
  color: #0f172a;
  border-bottom: 2.5px solid #2563eb;
  padding-bottom: 8px;
  margin-top: 30px;
  page-break-before: always;
}}
h1:first-of-type {{
  page-break-before: avoid;
  margin-top: 0;
}}
h2 {{
  font-size: 16.5px;
  color: #1e293b;
  border-bottom: 1px solid #cbd5e1;
  padding-bottom: 5px;
  margin-top: 22px;
  page-break-after: avoid;
}}
h3 {{
  font-size: 14px;
  color: #334155;
  margin-top: 16px;
  page-break-after: avoid;
}}
h4 {{
  font-size: 13px;
  color: #475569;
  margin-top: 12px;
  page-break-after: avoid;
}}
p, li {{
  line-height: 1.65;
  color: #334155;
}}
ul, ol {{
  padding-left: 20px;
}}
table {{
  border-collapse: collapse;
  width: 100%;
  margin: 12px 0;
  page-break-inside: avoid;
}}
th, td {{
  border: 1px solid #cbd5e1;
  padding: 6px 9px;
  font-size: 11.5px;
}}
th {{
  background-color: #f1f5f9;
  font-weight: 600;
  color: #0f172a;
  text-align: left;
}}
tr:nth-child(2n) {{
  background-color: #f8fafc;
}}
pre {{
  background: #f8fafc !important;
  border: 1px solid #e2e8f0;
  border-radius: 6px;
  padding: 10px 12px;
  font-size: 11px;
  line-height: 1.45;
  page-break-inside: avoid;
  white-space: pre-wrap !important;
  word-break: break-all;
}}
code {{
  font-family: "Consolas", "Courier New", monospace;
}}
p code, li code, td code {{
  background: #f1f5f9;
  padding: 2px 4px;
  border-radius: 3px;
  font-size: 11.5px;
  color: #e11d48;
}}
img {{
  max-width: 90%;
  max-height: 380px;
  object-fit: contain;
  display: block;
  margin: 10px auto;
  border-radius: 4px;
  border: 1px solid #cbd5e1;
  box-shadow: 0 1px 3px rgba(0,0,0,0.08);
  page-break-inside: avoid;
}}
blockquote {{
  border-left: 4px solid #2563eb;
  color: #334155;
  padding: 6px 12px;
  margin: 10px 0;
  background: #eff6ff;
  border-radius: 0 6px 6px 0;
}}
.mermaid {{
  text-align: center;
  margin: 16px 0;
  page-break-inside: avoid;
}}
hr {{
  border: 0;
  border-top: 1px solid #e2e8f0;
  margin: 20px 0;
}}
</style>
</head>
<body>
{html_body}
</body>
</html>
'''

    print(f"Writing intermediate HTML: {html_path}")
    with open(html_path, 'w', encoding='utf-8') as f:
        f.write(html_content)

    # 5. 调用 Edge 无头浏览器打印 PDF
    edge_paths = [
        r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
        r'C:\Program Files\Microsoft\Edge\Application\msedge.exe',
        r'C:\Program Files\Google\Chrome\Application\chrome.exe'
    ]
    browser_exe = None
    for p in edge_paths:
        if os.path.exists(p):
            browser_exe = p
            break

    if not browser_exe:
        raise RuntimeError("Neither Microsoft Edge nor Google Chrome was found on this system.")

    print(f"Using browser: {browser_exe}")
    html_url = f"file:///{os.path.abspath(html_path).replace(os.sep, '/')}"

    cmd = [
        browser_exe,
        '--headless',
        '--disable-gpu',
        '--no-pdf-header-footer',
        '--run-all-compositor-stages-before-draw',
        '--virtual-time-budget=6000',
        f'--print-to-pdf={pdf_path}',
        html_url
    ]

    print(f"Exporting PDF to: {pdf_path}")
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Browser stderr: {result.stderr}")
        raise RuntimeError(f"Browser failed with return code {result.returncode}")

    if os.path.exists(pdf_path):
        size_bytes = os.path.getsize(pdf_path)
        print(f"PDF generated successfully! File: {pdf_path}, Size: {size_bytes / (1024*1024):.2f} MB ({size_bytes} bytes)")
    else:
        raise RuntimeError("PDF output file was not created.")

if __name__ == '__main__':
    main()
