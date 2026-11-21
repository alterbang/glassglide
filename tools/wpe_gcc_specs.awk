/^\*cc1plus:$/ {
    print
    if (getline <= 0 || !length($0))
        exit 1
    print $0 " --param=ggc-min-expand=10 --param=ggc-min-heapsize=8192"
    found++
    next
}
{ print }
END {
    if (found != 1)
        exit 1
}
