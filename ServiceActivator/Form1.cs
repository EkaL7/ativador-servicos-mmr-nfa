using System.ServiceProcess;

namespace ServiceActivator;

public partial class Form1 : Form
{
    public Form1()
    {
        InitializeComponent();
    }

    private void BtnAdd_Click(object? sender, EventArgs e)
    {
        AddService();
    }

    private void TxtServiceName_KeyDown(object? sender, KeyEventArgs e)
    {
        if (e.KeyCode == Keys.Enter)
        {
            e.SuppressKeyPress = true;
            AddService();
        }
    }

    private void AddService()
    {
        var nome = txtServiceName.Text.Trim();
        if (string.IsNullOrEmpty(nome))
            return;

        foreach (DataGridViewRow row in dgvServices.Rows)
        {
            if (row.Cells[0].Value?.ToString()?.Equals(nome, StringComparison.OrdinalIgnoreCase) == true)
            {
                MessageBox.Show("Serviço já adicionado.", "Aviso", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
        }

        var status = VerificarStatus(nome);
        dgvServices.Rows.Add(nome, status);
        txtServiceName.Clear();
        txtServiceName.Focus();
    }

    private void BtnRemover_Click(object? sender, EventArgs e)
    {
        if (dgvServices.SelectedRows.Count > 0)
        {
            dgvServices.Rows.RemoveAt(dgvServices.SelectedRows[0].Index);
        }
        else
        {
            MessageBox.Show("Selecione um serviço para remover.", "Aviso", MessageBoxButtons.OK, MessageBoxIcon.Information);
        }
    }

    private async void BtnActivate_Click(object? sender, EventArgs e)
    {
        if (dgvServices.Rows.Count == 0)
        {
            MessageBox.Show("Adicione pelo menos um serviço.", "Aviso", MessageBoxButtons.OK, MessageBoxIcon.Information);
            return;
        }

        btnActivate.Enabled = false;
        btnAdd.Enabled = false;
        btnRemover.Enabled = false;
        progressBar.Visible = true;
        progressBar.Maximum = dgvServices.Rows.Count;
        progressBar.Value = 0;
        lblStatus.Text = "Iniciando...";

        for (int i = 0; i < dgvServices.Rows.Count; i++)
        {
            var row = dgvServices.Rows[i];
            var nomeServico = row.Cells[0].Value?.ToString() ?? "";

            row.Cells[1].Value = "Ativando...";
            dgvServices.FirstDisplayedScrollingRowIndex = i;
            Application.DoEvents();

            try
            {
                await Task.Run(() => AtivarServico(nomeServico));
                row.Cells[1].Value = "Ativado";
            }
            catch (Exception ex)
            {
                row.Cells[1].Value = $"Erro: {ex.Message}";
            }

            progressBar.Value = i + 1;
            lblStatus.Text = $"Processando: {nomeServico} ({i + 1}/{dgvServices.Rows.Count})";
        }

        lblStatus.Text = "Concluído!";
        btnActivate.Enabled = true;
        btnAdd.Enabled = true;
        btnRemover.Enabled = true;
    }

    private static string VerificarStatus(string nomeServico)
    {
        try
        {
            using var sc = new ServiceController(nomeServico);
            return sc.Status switch
            {
                ServiceControllerStatus.Running => "Ativado",
                ServiceControllerStatus.Stopped => "Desativado",
                ServiceControllerStatus.Paused => "Pausado",
                ServiceControllerStatus.StartPending => "Ativando...",
                ServiceControllerStatus.StopPending => "Desativando...",
                _ => sc.Status.ToString()
            };
        }
        catch (InvalidOperationException)
        {
            return "Não encontrado";
        }
        catch (Exception)
        {
            return "Erro ao verificar";
        }
    }

    private static void AtivarServico(string nomeServico)
    {
        using var sc = new ServiceController(nomeServico);
        if (sc.Status != ServiceControllerStatus.Running)
        {
            sc.Start();
            sc.WaitForStatus(ServiceControllerStatus.Running, TimeSpan.FromSeconds(30));
        }
    }
}
