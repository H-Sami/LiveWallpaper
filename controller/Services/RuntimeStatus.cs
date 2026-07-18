namespace LiveWallpaper.Controller.Services;

public sealed record RuntimeStatus(
    string RequestId,
    string Operation,
    string State,
    string ErrorCode,
    string Message)
{
    public static string PathForRequest(string basePath, string requestId) =>
        string.IsNullOrEmpty(requestId) ? basePath : $"{basePath}.{requestId}";

    public bool Matches(string requestId, string operation) =>
        string.Equals(RequestId, requestId, StringComparison.Ordinal) &&
        string.Equals(Operation, operation, StringComparison.OrdinalIgnoreCase);

    public static bool TryParse(string text, out RuntimeStatus? status)
    {
        status = null;
        var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        foreach (string rawLine in text.Split('\n'))
        {
            string line = rawLine.TrimEnd('\r');
            if (line.Length == 0) continue;
            int equals = line.IndexOf('=');
            if (equals <= 0) return false;
            try
            {
                values[line[..equals]] = Uri.UnescapeDataString(line[(equals + 1)..]);
            }
            catch (UriFormatException)
            {
                return false;
            }
        }
        if (!values.TryGetValue("version", out string? version) || version != "1" ||
            !values.TryGetValue("state", out string? state) || string.IsNullOrWhiteSpace(state)) return false;
        status = new RuntimeStatus(
            values.GetValueOrDefault("request_id", string.Empty),
            values.GetValueOrDefault("operation", string.Empty),
            state,
            values.GetValueOrDefault("error_code", string.Empty),
            values.GetValueOrDefault("message", string.Empty));
        return true;
    }
}
