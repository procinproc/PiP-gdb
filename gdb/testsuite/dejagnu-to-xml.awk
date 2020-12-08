#!/usr/bin/awk -f

BEGIN {
	test_class = ENVIRON["DEJAGNU_TEST_CLASS"]
	print "<testsuite>"
	script =""
	msg = ""
} 
/^Running / {
	if (script != "" && msg != "") {
		printf "  <testcase classname=\"%s\" name=\"%s\">\n",
			test_class, script
		printf "    <error type=\"CANNOT_EXECUTE\"/>\n"
		printf "    <system-err><![CDATA[%s]]></system-err>\n", msg
		printf "  </testcase>\n"
	}
	script = $0
	sub(/^Running /, "", script)
	msg = ""
	next
}
/^(PASS|FAIL|XPASS|XFAIL|KFAIL|UNRESOLVED|UNTESTED|UNSUPPORTED): / {
	result = $1
	test = $0
	sub(/^[^:]*: /, "", test)
	printf "  <testcase classname=\"%s\" name=\"%s\">\n", test_class, test
	if (result ~ /^(FAIL|XPASS|XFAIL|KFAIL):$/) {
		printf "    <failure type=\"%s\"/>\n",
			substr(result, 1, length(result) - 1)
	} else if (result ~ /^(UNTESTED|UNSUPPORTED):$/) {
		printf "    <skipped/>\n"
	} else if (result == "UNRESOLVED:") {
		printf "    <error type=\"%s\"/>\n",
			substr(result, 1, length(result) - 1)
	} else { /* result == "PASS:" */
		/* do nothing */
	}
	if (msg != "") {
		printf "    <system-err><![CDATA[%s]]></system-err>\n", msg
	}
	printf "  </testcase>\n"
	msg = ""
	next
}
/=== gdb Summary ===/ {
	if (script != "" && msg != "" && msg != "\n") {
		printf "  <testcase classname=\"%s\" name=\"%s\">\n",
			test_class, script
		printf "    <error type=\"CANNOT_EXECUTE\"/>\n"
		printf "    <system-err><![CDATA[%s]]></system-err>\n", msg
		printf "  </testcase>\n"
	}
	exit 0
}
{
	if (msg != "") msg = msg "\n"
	msg = msg $0
}
END { print "</testsuite>" } 
