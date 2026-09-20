$l = Get-Content 'F:\9ymsgo\9you\viewer\_fld_log.txt'
$ok = ($l | Where-Object { $_ -like 'OK *' }).Count
$fail = ($l | Where-Object { $_ -like 'FAIL *' }).Count
$other = ($l | Where-Object { $_ -notlike 'OK *' -and $_ -notlike 'FAIL *' }).Count
Write-Host ("lines=$($l.Count) OK=$ok FAIL=$fail other=$other")
$l | Where-Object { $_ -like 'FAIL *' } | Select-Object -First 10
