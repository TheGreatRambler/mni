# TooTiny
A programming language that is simply too tiny

## Variable integer sizes
According to Github data, these are the approximate usages of different integer sizes and common datatypes
* int8_t: 12M
* uint8_t: 104M
* int16_t: 16M
* uint16_t: 69M
* int32_t (also referred to as int, inflates number): 41M
* uint32_t: 165M
* int64_t: 26M
* uint64_t: 66M
* __int128_t: 171K
* __uint128_t: 365K
* float: 451M
* double: 374M
* boolean: 457M
* std::string: 11M
* std::vector: 21M
These values form the basis of a binary tree used to optimize datatypes
```
__int128_t
__uint128_t
std::string
int8_t
int16_t
std::vector
int64_t
uint64_t
uint16_t
uint8_t
uint32_t
double
float
boolean
int32_t
```