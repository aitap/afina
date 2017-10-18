#!/usr/bin/perl
use 5.020;
use warnings;
use threads;
use threads::shared;
use IPC::Open3;
use Test::More tests => 25;
use IO::Socket::INET;
use Getopt::Long;

my $backend = "epoll";
my $silent = 0;

GetOptions(
	"backend=s" => \$backend,
	"silent" => \$silent
) or die "Usage: $0 [-backend=backend] [--silent]\n";

my $pid = open3(my $stdin, my $stdout, 0, "src/afina", "-n", $backend);
ok $pid, "Started afina with PID=$pid and $backend backend";

local $SIG{ALRM} = sub { kill 'KILL', $pid; die "Timeout\n" };
alarm(5);
while (<$stdout>) {
	print "# STDOUT: $_";
	if (/network debug: waiting for connection\.\.\./) {
		alarm(0);
		ok 1, "Afina is waiting for connections";
		last;
	}
}
ok(close($stdin), "Putting Afina to background");
(threads::->create(sub { my $fh = $_[0]; while(<$fh>) { $silent || print "# afina: $_" } }, $stdout))->detach();

alarm(10);

sub afina_request_silent { # 0 tests
	my ($request) = @_;
	my $socket = IO::Socket::INET::->new(
		PeerAddr => "127.0.0.1:8080",
		Proto => "tcp"
	) or die "socket: $!";
	print($socket $request) or die "print: $!";
	shutdown($socket, SHUT_WR()) or die "shutdown: $!";
	my $received;
	$received .= $_ while (<$socket>);
	$received;
}

sub afina_request { # 3 tests
	my ($request) = @_;
	my $socket = IO::Socket::INET::->new(
		PeerAddr => "127.0.0.1:8080",
		Proto => "tcp"
	);
	ok($socket, "Connected to Afina");
	ok(print($socket $request), "Sent request");
	print $request =~ s/^/# -> /mrg;
	ok(shutdown($socket, SHUT_WR()), "Closed writing end of connection");
	my $received;
	$received .= $_ while (<$socket>);
	print $received =~ s/^/# <- /mrg;
	$received;
}

sub afina_test { # 4 tests
	my ($request, $response) = @_;
	my $received = afina_request($request);
	is($received, $response, "Response matches expected");
}

afina_test("set foo 0 0 6\r\nfoobar\r\n", "STORED\r\n");
afina_test("get foo\r\n", "VALUE foo 0 6\r\nfoobar\r\nEND\r\n");
afina_test("get foo\r\nget foo\r\n", "VALUE foo 0 6\r\nfoobar\r\nEND\r\nVALUE foo 0 6\r\nfoobar\r\nEND\r\n");

my %par_responses;
$par_responses{$_}++ for (map { $_->join } map { threads::->create(\&afina_request_silent, $_) } map { sprintf "set bar 0 0 3\r\n%03d\r\n", $_ } 1..100);
say "# Parallel test responses:";
for (sort { $par_responses{$a} <=> $par_responses{$b} } keys %par_responses) {
	print "# $par_responses{$_} ".($_=~s/([\r\n])/{"\r"=>'\r',"\n"=>'\n'}->{$1}/megr)."\n"
}
ok($par_responses{"STORED\r\n"}, "Afina replied with 'STORED' at least once");

afina_test(
	"set foo 0 0 3\r\nwtf\r\n"
	."get foo\r\n",
	"STORED\r\n"
	."VALUE foo 0 3\r\nwtf\r\nEND\r\n"
);

afina_test(
	"set foo 0 0 3\r\nwtf\r\n"
	."set bar 0 0 3\r\nzzz\r\n"
	."get foo bar\r\n",
	"STORED\r\n"
	."STORED\r\n"
	."VALUE foo 0 3\r\nwtf\r\n"
	."VALUE bar 0 3\r\nzzz\r\n"
	."END\r\n"
);

ok(kill('KILL', $pid), "Stopped Afina");

