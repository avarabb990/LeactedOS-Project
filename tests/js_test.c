/* Unit tests for js.h */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t i32;
#include "../js.h"
static char out[8192]; static int outn; static char alerts[2048]; static u32 clock_ms = 1000, rs = 1;
static void h_console(const char *s) { outn += snprintf(out + outn, sizeof out - (size_t)outn, "%s\n", s); }
static void h_alert(const char *s) { strncat(alerts, s, sizeof alerts - strlen(alerts) - 2); strcat(alerts, "|"); }
static u32 h_now(void) { return clock_ms; }
static u32 h_rnd(void) { rs = rs * 1103515245u + 12345u; return rs >> 8; }
static int h_date(int *f) { f[0] = 2026; f[1] = 11; f[2] = 25; f[3] = 9; f[4] = 5; f[5] = 7; f[6] = 3; return 1; }
static struct jshost host = { 0, 0, 0, h_console, h_alert, h_now, h_rnd, h_date };
static int fails, total;
static const char *run(const char *src) {                      /* run a script, return what console.log printed (or the error) */
    js_init(&host); outn = 0; out[0] = 0; alerts[0] = 0;
    int rc = js_run(src, (int)strlen(src));
    if (rc) { snprintf(out + outn, sizeof out - (size_t)outn, "ERR:%s", js_errmsg); }
    else if (outn && out[outn - 1] == '\n') out[outn - 1] = 0;
    return out;
}
#define T(src, want) do { total++; const char *g = run(src); if (strcmp(g, want)) { printf("FAIL: %s\n   want: %s\n   got:  %s\n", src, want, g); fails++; } } while (0)
int main(void) {
    T("console.log(1+2*3)", "7");
    T("console.log(10/4, 7%3, -7%3, 2**10)", "2.5 1 -1 1024");
    T("console.log(0.1+0.2, 1/3, 100/7)", "0.3 0.33333333 14.28571429");
    T("console.log('a'+1, 1+'2', '3'*'4', true+1, null+1, undefined+1)", "a1 12 12 2 1 NaN");
    T("var x = 5; let y = 'hi'; const z = [1,2,3]; console.log(x, y, z)", "5 hi 1,2,3");
    T("function fib(n){ return n<2?n:fib(n-1)+fib(n-2) } console.log(fib(20))", "6765");
    T("var s=0; for (var i=0;i<100;i++){ if(i%2) continue; if(i>50) break; s+=i } console.log(s)", "650");
    T("var o = {a:1, b:{c:[1,2,{d:'x'}]}}; console.log(o.b.c[2].d, o['a'], JSON.stringify(o))", "x 1 {\"a\":1,\"b\":{\"c\":[1,2,{\"d\":\"x\"}]}}");
    T("var a=[3,1,2]; a.push(5); a.sort(); console.log(a.join('-'), a.length, a.indexOf(5))", "1-2-3-5 4 3");
    T("console.log([1,2,3].map(x=>x*2).filter(x=>x>2).reduce((a,b)=>a+b,0))", "10");
    T("var f=(a,b=10)=>a+b; console.log(f(1), f(1,2), typeof f, typeof 1, typeof 'a', typeof undefinedVar, typeof null)", "11 3 function number string undefined object");
    T("function mk(){ var c=0; return function(){ return ++c } } var c1=mk(), c2=mk(); c1(); c1(); console.log(c1(), c2())", "3 1");
    T("var fs=[]; for (let i=0;i<3;i++) fs.push(()=>i); console.log(fs[0](), fs[1](), fs[2]())", "0 1 2");
    T("var fs=[]; for (var i=0;i<3;i++) fs.push(()=>i); console.log(fs[0](), fs[2]())", "3 3");
    T("console.log(`a${1+1}b${'c'}d`, `x`, `${'q'}`)", "a2bcd x q");
    T("var n=0; try { null.x } catch(e) { n=1; console.log(e.name, e.message) } finally { console.log('fin', n) }", "TypeError Cannot read properties of null (reading 'x')\nfin 1");
    T("try { throw new Error('boom') } catch (e) { console.log(e.message) }", "boom");
    T("function f(){ try { return 1 } finally { console.log('cleanup') } } console.log(f())", "cleanup\n1");
    T("switch (3) { case 1: console.log('one'); case 3: console.log('three'); case 4: console.log('four'); break; default: console.log('d') }", "three\nfour");
    T("var i=0; while(true){ i++; if(i>5) break } do { i-- } while(i>3); console.log(i)", "3");
    T("var o={x:1,y:2}; var ks=[]; for (var k in o) ks.push(k); for (var v of [7,8]) ks.push(v); console.log(ks)", "x,y,7,8");
    T("console.log('Hello'.toUpperCase(), 'abc'.slice(1), 'a,b,c'.split(',').length, ' x '.trim(), 'abcabc'.replace('b','X'), 'abc'.indexOf('c'), 'x'.repeat(3), '5'.padStart(3,'0'))", "HELLO bc 3 x aXcabc 2 xxx 005");
    T("console.log((3.14159).toFixed(2), (2.5).toFixed(0), (1234).toString(), (255).toString(16), parseInt('42px'), parseFloat('3.5abc'), parseInt('ff',16), Number('12'), Number('x'))", "3.14 3 1234 ff 42 3.5 255 12 NaN");
    T("console.log(Math.floor(2.7), Math.ceil(2.1), Math.round(2.5), Math.round(-2.5), Math.abs(-3), Math.max(1,5,3), Math.min(4,2), Math.sqrt(16), Math.pow(2,8), Math.floor(Math.PI*100))", "2 3 3 -2 3 5 2 4 256 314");
    T("console.log(Math.sin(0), Math.cos(0), Math.floor(Math.sin(Math.PI/2)*1000), Math.round(Math.cos(Math.PI)*1000))", "0 1 1000 -1000");
    T("var r=Math.random(); console.log(r>=0 && r<1)", "true");
    T("console.log(1==1, 1=='1', 1==='1', null==undefined, null===undefined, NaN==NaN, 'a'<'b', 2<10, '2'<'10')", "true true false true false false true true false");
    T("var a=null; console.log(a?.b, a ?? 'dflt', 0 || 'x', 0 ?? 'y', 1 && 2, !!'')", "undefined dflt x 0 2 false");
    T("console.log(5&3, 5|3, 5^3, ~5, 1<<4, -16>>2, -1>>>28)", "1 7 6 -6 16 -4 15");
    T("var i=1; i+=2; i*=3; i-=1; i/=2; i%=3; console.log(i, i++, ++i, i--, i)", "1 1 3 3 2");
    T("var o={n:0, inc(){ this.n++; return this }}; o.inc().inc(); console.log(o.n)", "2");
    T("function P(n){ this.n=n } var p = new P(7); console.log(p.n)", "7");
    T("var a=[1,2,3,4,5]; console.log(a.slice(1,3), a.splice(1,2), a, a.concat([9]), [1,[2,3]].join(';'))", "2,3 2,3 1,4,5 1,4,5,9 1;2,3");
    T("console.log([...'ab'], [...[1,2],3], Math.max(...[4,9,2]))", "a,b 1,2,3 9");
    T("var t=0; setTimeout(function(){ t=5 }, 100); console.log(t)", "0");
    T("console.log(Object.keys({a:1,b:2}), Object.values({a:1,b:2}), Array.isArray([]), Array.isArray({}))", "a,b 1,2 true false");
    T("console.log(String(12), Number('7')+1, Boolean(''), Boolean('x'), [1,2]+'', {}+'')", "12 8 false true 1,2 [object Object]");
    T("console.log(new Date().getFullYear(), new Date().getHours(), Date.now()>0)", "2026 9 true");
    T("var x = 10; { let x = 20 } console.log(x)", "20");   /* block scope is not modelled: documented limitation */
    T("function f(){ return g() } function g(){ return 'hoisted' } console.log(f())", "hoisted");
    T("var s=''; for (var i=0;i<5;i++) s+=i; console.log(s, s.length, s[2], s.charAt(3), s.charCodeAt(0))", "01234 5 2 3 48");
    T("console.log(typeof alert, isNaN('x'), isNaN('5'), isFinite(1/0))", "function true false false");
    T("alert('hi ' + 1)", "");
    T("var u; console.log(u, u === undefined, [undefined, null].join(), (function(){})())", "undefined true , undefined");
    T("console.log([3,20,100].sort(), [3,20,100].sort((a,b)=>a-b), ['b','a'].reverse())", "100,20,3 3,20,100 a,b");
    T("console.log(JSON.stringify([1,'a',null,true,{x:undefined}]), JSON.stringify('q\"'), JSON.stringify({a:[1,2]}, null, 2))", "[1,\"a\",null,true,{}] \"q\\\"\" {\n  \"a\": [\n    1,\n    2\n  ]\n}");
    /* errors */
    T("console.log(1", "ERR:SyntaxError: expected ')' (line 1)");
    T("var = 5", "ERR:SyntaxError: expected a variable name (destructuring is not supported) (line 1)");
    T("undefinedFn()", "ERR:Uncaught ReferenceError: undefinedFn is not defined");
    T("var o={}; o.f()", "ERR:Uncaught TypeError: f is not a function");
    T("throw 'plain'", "ERR:Uncaught plain");
    T("while(true){}", "ERR:the script ran too long and was stopped");
    T("function f(){ return f() } f()", "ERR:Uncaught RangeError: Maximum call stack size exceeded");
    T("class A {}", "ERR:SyntaxError: classes are not supported (line 1)");
    T("var s='x'; for (var i=0;i<40;i++) s=s+s; console.log(s.length)", "ERR:out of memory (strings)");
    /* resource limits: scope rollback keeps recursion cheap */
    T("function add(a,b){ return a+b } var s=0; for (var i=0;i<100000;i++) s=add(s,1); console.log(s)", "100000");
    /* timers */
    js_init(&host); outn = 0; out[0] = 0;
    { const char *ts = "var n=0; var id=setInterval(function(){ n++; console.log('tick',n); if(n==3) clearInterval(id) }, 50); setTimeout(function(){ console.log('once') }, 120);"; js_run(ts, (int)strlen(ts)); }
    total++; int ran = 0; for (int k = 0; k < 10; k++) { clock_ms += 50; ran += js_poll(); }
    if (strcmp(out, "tick 1\ntick 2\nonce\ntick 3\n") || ran != 4 || js_timers_active()) { printf("FAIL timers: %s (ran %d)\n", out, ran); fails++; }
    /* fuzz: random token soup must never crash */
    srand(7);
    static const char *bits[] = { "var ", "x", "=", "1", ";", "(", ")", "{", "}", "[", "]", "function ", "return ", "if", "else", "for", "while", "'s'", "`a${", "}`", ".", ",", "=>", "+", "++", "?", ":", "new ", "try", "catch", "throw ", "\n", "/*", "*/", "\"", "0x", "1e9", "..." };
    for (int it = 0; it < 4000; it++) {
        char b[400]; int o = 0, k = 1 + rand() % 40;
        for (int j = 0; j < k; j++) { const char *t = bits[rand() % (int)(sizeof bits / sizeof bits[0])]; int l = (int)strlen(t); if (o + l < 390) { memcpy(b + o, t, (size_t)l); o += l; } }
        b[o] = 0; js_init(&host); outn = 0; js_run(b, o);
    }
    printf("js test: %d checks, %s\n", total, fails ? "FAILED" : "ok");
    return fails != 0;
}
