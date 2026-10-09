package sample
fun warns(s: String): Int {
    val unused = 1
    return s?.length ?: 0   // UNNECESSARY_SAFE_CALL / USELESS_ELVIS
}
@Deprecated("old") fun old() = 1
fun callsDeprecated() = old()   // DEPRECATION
