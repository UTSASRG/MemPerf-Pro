$1 == "user" {
	sub(/^0m/, "", $2);
	sub(/s$/, "", $2);
	user += $2;
}

$1 == "sys" {
	sub(/^0m/, "", $2);
	sub(/s$/, "", $2);
	sys += $2;
}

END {
	print "user=" user ", sys=" sys ", total=" (user+sys);
}
