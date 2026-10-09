package negative

fun warns(s: String): Int {
    return s?.length ?: 0
}

@Deprecated("old")
fun old() = 1

fun callsDeprecated() = old()
