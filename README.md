# Linux 시스템 호출 추적기

`ft_strace`는 실행한 프로그램이 Linux 커널에 어떤 작업을 요청했고, 커널이 무엇을 돌려줬는지 보여 주는 C 도구입니다. 예를 들어 프로그램이 파이프에서 4바이트를 읽었다면 `read`에 전달한 버퍼 주소와 요청 길이만으로는 그 내용을 알 수 없습니다. 이 추적기는 호출이 끝난 시점의 버퍼를 읽어 실제 데이터와 반환값을 함께 기록합니다.

## 직접 확인하기

Ubuntu 22.04 x86-64(WSL2)에서 검증했습니다. 빌드에는 GCC와 GNU Make가 필요하며, 커널이 부모 프로세스의 자식에 대한 `ptrace`와 메모리 읽기를 허용해야 합니다. 저장소 최상위에서 다음 예제를 실행합니다.

```sh
make
demo_dir=$(mktemp -d)
gcc portfolio_audit/tests/t03_read_pipe.c -o "$demo_dir/read-pipe"
./ft_strace "$demo_dir/read-pipe" 2>"$demo_dir/trace.log"
grep -F '"READ", 4)' "$demo_dir/trace.log"
```

예제는 버퍼를 `XXXX`로 채운 뒤 파이프에 쓴 `READ` 네 바이트를 그 버퍼로 읽습니다. 추적 기록에서 다음과 같은 줄을 확인할 수 있습니다. 첫 번째 숫자는 파일 디스크립터라서 실행 환경에 따라 달라질 수 있습니다.

```text
read(3, "READ", 4) = 0x4
```

다른 프로그램에도 같은 방식으로 실행 경로와 인자를 넘깁니다. 예를 들어 `./ft_strace /bin/echo hello`를 실행하면 `hello`는 표준 출력으로, 추적 기록은 표준 오류로 나옵니다. 대상은 `/bin/echo` 또는 `./program`처럼 경로로 지정합니다. 추적기가 자식의 종료 코드를 전달하므로 셸 스크립트에서도 대상의 성공 여부를 확인할 수 있습니다.

## 한 호출이 기록되기까지

1. 자식이 실행 전 `SIGSTOP`으로 멈추면 부모가 `PTRACE_SEIZE`로 추적을 시작합니다. 이후 `PTRACE_SYSCALL`로 자식을 재개하고 `waitpid`로 다음 정지를 기다립니다.
2. 시스템 호출에 진입하면 `PTRACE_GETREGSET`으로 레지스터를 읽습니다. 레지스터 집합의 크기로 x86-64와 i386을 구분하고, 해당 ABI의 표에서 호출 번호에 맞는 이름과 인자 형식을 찾습니다. [호출 정의와 인자 표](include/strace_data.h)
3. `write`의 입력 바이트는 진입 시점에, `read`의 출력 바이트는 커널이 반환한 뒤에 `process_vm_readv`로 읽습니다. 성공한 `read`는 요청 길이와 실제 반환 길이 중 작은 값까지 표시하고, 바이트 덤프는 최대 64바이트로 제한합니다. [추적 루프와 출력 구현](ft_strace.c)

## 구현하면서 해결한 문제

- 첫 구현은 `read`에 진입하는 순간 버퍼를 읽어 위 예제에서 `XXXX`를 출력했습니다. 진입할 때 인자 레지스터를 저장하고 출력 버퍼 해석을 반환 시점까지 미뤘습니다. 부분 읽기와 EOF를 따로 검사해 반환 길이 밖의 데이터가 섞이는 경우도 확인했습니다. [버퍼 경계 테스트](portfolio_audit/tests/read_buffer_edges.c)
- i386에서 잘못된 주소로 `write`를 호출했을 때 32비트 음수 오류값이 큰 양수처럼 표시됐습니다. ABI별로 반환 레지스터의 부호를 해석하고, 읽을 수 없는 주소는 주소값 그대로 표시하도록 했습니다. 64비트 `lseek`에서는 4GiB를 넘는 오프셋의 상위 비트가 유지되는지도 검사합니다. [회귀 테스트](portfolio_audit/tests/test_trace_regressions.py)
- `ptrace`의 정지·재개를 작은 예제로 먼저 확인하고, x86-64와 i386의 호출 번호·이름·인자 형식을 각각 한 목록에 모았습니다. 새 행을 추가하면 같은 정의에서 번호와 조회 표가 생성됩니다. [단계별 추적 실험](Test/ptrace_tutorial/)

## 테스트와 추적 범위

```sh
python3 portfolio_audit/tests/test_trace_regressions.py -v
```

테스트에는 Python 3, GCC, GNU binutils가 필요합니다. i386 사례는 32비트 ELF를 실행할 수 있는 Linux 커널에서 동작합니다. Ubuntu 22.04에서 버퍼, errno, 오프셋, 인자 전달, 정상·시그널 종료를 포함한 회귀 테스트 **9개가 모두 통과**했습니다.

별도로 기존 `strace`와 비교한 15개 사례 중 11개가 일치했습니다. 3개는 `newfstatat`의 구조체, 시그널 정보, `socketpair`의 결과 배열을 표시하는 상세도에 차이가 있었습니다. 나머지 1개는 자손 프로세스까지 추적하는 `strace -f` 비교입니다. 이 도구의 추적 대상은 직접 실행한 자식 하나입니다. [수정 전 비교 기록](portfolio_audit/test_results.md) · [실패를 재현한 입력과 원인](portfolio_audit/failures.md)
