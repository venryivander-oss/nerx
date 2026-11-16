$l = New-Object System.Net.HttpListener
$l.Prefixes.Add("http://localhost:8765/")
$l.Start()
while ($l.IsListening) {
  $c = $l.GetContext()
  $req = $c.Request
  $body = ""
  if ($req.HasEntityBody) { $sr = New-Object IO.StreamReader($req.InputStream, [Text.Encoding]::UTF8); $body = $sr.ReadToEnd() }
  $path = $req.Url.PathAndQuery
  $resp = "metode=" + $req.HttpMethod + " path=" + $path + " ctype=" + $req.ContentType + " auth=" + $req.Headers["X-Tes"] + " body=" + $body
  $code = 200
  if ($path -like "/404*") { $code = 404 }
  $bytes = [Text.Encoding]::UTF8.GetBytes($resp)
  $c.Response.StatusCode = $code
  $c.Response.OutputStream.Write($bytes, 0, $bytes.Length)
  $c.Response.Close()
}
