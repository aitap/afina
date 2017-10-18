#!/usr/bin/perl
use 5.020;
use warnings;
use threads;
use IPC::Open3;
use Test::More tests => 12;
use IO::Socket::INET;

my $pid = open3(my $stdin, my $stdout, 0, qw(src/afina -n blocking));
ok $pid, "Started afina with PID=$pid";

local $SIG{ALRM} = sub { die "Timeout\n" };
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
(threads::->create(sub { my $fh = $_[0]; while(<$fh>) { print "# afina: $_" } }, $stdout))->detach();

sub afina_request {
	my ($request, $response) = @_;
	my $socket = IO::Socket::INET::->new(
		PeerAddr => "127.0.0.1:8080",
		Proto => "tcp"
	);
	ok($socket, "Connected to Afina");
	print $request =~ s/^/# -> /mrg;
	ok(print($socket $request), "Sent request");
	ok(shutdown($socket, SHUT_WR()), "Closed writing end of connection");
	my $received;
	$received .= $_ while (<$socket>);
	print $received =~ s/^/# <- /mrg;
	is($received, $response, "Response matches expected");
}

afina_request("set foo 0 0 6\r\nfoobar\r\n", "STORED\r\n");
afina_request("get foo\r\n", "VALUE foo 0 6\r\nfoobar\r\nEND\r\n");

ok(kill('KILL', $pid), "Stopped Afina");

