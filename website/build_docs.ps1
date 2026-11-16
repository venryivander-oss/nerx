# build_docs.ps1: membuat docs.html dari ..\SYNTAX.en.txt. Jalankan:
#   powershell -ExecutionPolicy Bypass -File website\build_docs.ps1
$root = Split-Path $PSScriptRoot -Parent
$lines = ([IO.File]::ReadAllText("$root\SYNTAX.en.txt").Replace("`r`n","`n")) -split "`n"
function Esc($s) { $s.Replace('&','&amp;').Replace('<','&lt;').Replace('>','&gt;').Replace('"','&quot;').Replace("'","&#x27;") }
function Slug($s) { ([regex]::Replace($s.ToLower(), '[^a-z0-9]+', '-')).Trim('-') }
$sections = New-Object System.Collections.ArrayList
$cur = $null
$i = 0
while ($i -lt $lines.Count) {
    $ln = $lines[$i]
    if ($ln.StartsWith('====') -and ($i + 2) -lt $lines.Count -and $lines[$i + 2].StartsWith('====')) {
        $title = $lines[$i + 1].Trim()
        $m = [regex]::Match($title, '^(\d+)\.\s+(.*)')
        if ($m.Success) {
            $t = $m.Groups[2].Value
            $name = $t.Substring(0,1).ToUpper() + $t.Substring(1).ToLower()
            $name = $name.Replace('Vs code','VS Code').Replace('(no vm)','(no VM)').Replace('Nrxc:','nrxc:')
            $sec = @{ name = $name; id = (Slug $name); subs = (New-Object System.Collections.ArrayList) }
            [void]$sections.Add($sec)
            $cur = @{ name = $null; id = $null; body = (New-Object System.Collections.ArrayList) }
            [void]$sec.subs.Add($cur)
        }
        $i += 3; continue
    }
    if ($ln.StartsWith('----') -and ($i + 2) -lt $lines.Count -and $lines[$i + 2].StartsWith('----') -and $sections.Count -gt 0) {
        $n = $lines[$i + 1].Trim()
        $cur = @{ name = $n; id = (Slug $n); body = (New-Object System.Collections.ArrayList) }
        [void]$sections[$sections.Count - 1].subs.Add($cur)
        $i += 3; continue
    }
    if ($cur -ne $null) { [void]$cur.body.Add($ln) }
    $i++
}
function Render($body) {
    $out = New-Object System.Collections.ArrayList
    $blocks = New-Object System.Collections.ArrayList
    $blk = New-Object System.Collections.ArrayList
    foreach ($l in $body) {
        if ($l.Trim() -eq '') { if ($blk.Count) { [void]$blocks.Add($blk); $blk = New-Object System.Collections.ArrayList } }
        else { [void]$blk.Add($l) }
    }
    if ($blk.Count) { [void]$blocks.Add($blk) }
    foreach ($b in $blocks) {
        $h = [regex]::Match($b[0].Trim(), '^--- (.+) ---$')
        if ($b.Count -eq 1 -and $h.Success) { [void]$out.Add('<h4>' + (Esc $h.Groups[1].Value) + '</h4>'); continue }
        $all2 = $true; $all4 = $true
        foreach ($l in $b) { if (-not $l.StartsWith('  ')) { $all2 = $false }; if (-not $l.StartsWith('    ')) { $all4 = $false } }
        if ($all2) {
            $width = ($b | ForEach-Object { $_.Length - $_.TrimStart().Length } | Measure-Object -Minimum).Minimum
            $text = ($b | ForEach-Object { $_.Substring($width) }) -join "`n"
            $cls = if ($all4) { 'prog' } else { 'ref' }
            [void]$out.Add("<pre class=`"$cls`">" + (Esc $text) + '</pre>')
        }
        elseif ($b[0].StartsWith('- ')) {
            $items = New-Object System.Collections.ArrayList; $item = New-Object System.Collections.ArrayList
            foreach ($l in $b) {
                if ($l.StartsWith('- ')) { if ($item.Count) { [void]$items.Add(($item -join ' ')) }; $item = New-Object System.Collections.ArrayList; [void]$item.Add($l.Substring(2).Trim()) }
                else { [void]$item.Add($l.Trim()) }
            }
            [void]$items.Add(($item -join ' '))
            [void]$out.Add('<ul>' + (($items | ForEach-Object { '<li>' + (Esc $_) + '</li>' }) -join '') + '</ul>')
        }
        else { [void]$out.Add('<p>' + (Esc (($b | ForEach-Object { $_.Trim() }) -join ' ')) + '</p>') }
    }
    return ($out -join "`n")
}
$toc = New-Object System.Collections.ArrayList; $doc = New-Object System.Collections.ArrayList
foreach ($s in $sections) {
    [void]$toc.Add('<li><a href="#' + $s.id + '">' + (Esc $s.name) + '</a></li>')
    [void]$doc.Add('<section class="part" data-part>')
    [void]$doc.Add('<h2 id="' + $s.id + '">' + (Esc $s.name) + '</h2>')
    foreach ($sub in $s.subs) {
        if ($sub.name) {
            $short = ($sub.name -split '  -  ')[0]
            [void]$toc.Add('<li class="sub"><a href="#' + $sub.id + '">' + (Esc $short) + '</a></li>')
            [void]$doc.Add('<h3 id="' + $sub.id + '">' + (Esc $sub.name) + '</h3>')
        }
        [void]$doc.Add((Render $sub.body))
    }
    [void]$doc.Add('</section>')
}
$page = [IO.File]::ReadAllText("$root\website\docs_template.html")
$page = $page.Replace('{{TOC}}', ($toc -join "`n")).Replace('{{DOC}}', ($doc -join "`n"))
[IO.File]::WriteAllText("$root\website\docs.html", $page.Replace("`r`n","`n"), (New-Object Text.UTF8Encoding $false))
Write-Host ("docs.html: {0} sections" -f $sections.Count)